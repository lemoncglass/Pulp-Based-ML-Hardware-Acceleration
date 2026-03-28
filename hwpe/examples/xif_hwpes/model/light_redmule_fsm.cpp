/*
 * LightRedMulE — FSM Handler
 * ============================
 *
 * This is the cycle-driven finite state machine that orchestrates the
 * entire GEMM computation.  It is registered as a clock event and runs
 * once per cycle while the accelerator is active.
 *
 * State transitions:
 *
 *   IDLE ──(trigger)──► PRELOAD ──► ROUTINE ──┐
 *                                              │ (loop over tile triples)
 *                                              ├──► ROUTINE
 *                                              │
 *                          ┌───────────────────┘ (last iteration done)
 *                          ▼
 *                       STORING ──► FINISHED ──► ACKNOWLEDGE ──► IDLE
 *                                       │
 *                                       └──(XIF mode, no pending query)──► IDLE
 *
 * Each state issues TCDM requests one per cycle, tracks outstanding
 * responses via pending_req_queue, and advances to the next state
 * once all blocks have been transferred and all responses received.
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#include "light_redmule.h"

void LightRedmule::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    LightRedmule *_this = (LightRedmule *)__this;

    _this->fsm_timestamp += 1;

    switch (_this->state.get()) {

    /* ================================================================
     *  IDLE — should not normally fire, but clear IRQ if it does.
     * ================================================================ */
    case IDLE:
        _this->done.sync(false);
        break;

    /* ================================================================
     *  PRELOAD — Load initial X and Y tiles into local buffers.
     *  Once complete, transition to ROUTINE.
     * ================================================================ */
    case PRELOAD: {
        /* --- Send one TCDM request per cycle --- */
        if ((_this->fsm_counter < _this->tcdm_block_total) &&
            (_this->pending_req_queue.size() <= _this->queue_depth))
        {
            uint32_t temp_addr = _this->next_addr() - _this->loc_base;
            _this->tcdm_req->init();
            _this->tcdm_req->set_addr(temp_addr);
            _this->tcdm_req->set_data(_this->access_buffer);

            vp::IoReqStatus err = _this->send_tcdm_req();
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Preload] Send TCDM req #%d [addr=0x%08x]\n",
                _this->fsm_counter, temp_addr);

            if (err != vp::IO_REQ_OK) {
                _this->trace.fatal("[LightRedmule][Preload] TCDM error\n");
                return;
            }

            /* Process data (copy into local buffer) if compute enabled */
            if (_this->compute_able != 0)
                _this->process_iter_instruction();

            uint32_t receive_stamp = _this->tcdm_req->get_latency() + _this->fsm_timestamp;
            _this->pending_req_queue.push(receive_stamp);
            _this->fsm_counter += 1;
        }

        /* --- Drain completed responses --- */
        while ((_this->pending_req_queue.size() != 0) &&
               (_this->pending_req_queue.front() <= _this->fsm_timestamp)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Preload] Receive TCDM resp\n");
            _this->pending_req_queue.pop();
        }

        /* --- Transition when all blocks done --- */
        if ((_this->fsm_counter >= _this->tcdm_block_total) &&
            (_this->pending_req_queue.size() == 0))
        {
            _this->tcdm_block_total = _this->get_routine_access_block_number();
            _this->fsm_counter   = 0;
            _this->fsm_timestamp = 0;
            _this->state.set(ROUTINE);

            /* Forward Y→Z at Preload→Routine boundary if computing */
            if (_this->compute_able != 0) {
                _this->iter_instruction = INSTR_FORWARD_YZ;
                _this->process_iter_instruction();
            }
        } else {
            _this->state.set(PRELOAD);
        }

        _this->event_enqueue(_this->fsm_event, 1);
        break;
    }

    /* ================================================================
     *  ROUTINE — Main computation loop.
     *  For each tile triple (i,j,k):
     *    1. Load W tile (row by row)
     *    2. Compute Z += X · W  (overlapped with W loading)
     *    3. Prefetch next X tile
     *    4. Prefetch next Y tile (if k wraps)
     *    5. Store previous Z tile (if k==0 and not first iter)
     * ================================================================ */
    case ROUTINE: {
        /* --- Send one TCDM request per cycle --- */
        if ((_this->fsm_counter < _this->tcdm_block_total) &&
            (_this->pending_req_queue.size() <= _this->queue_depth))
        {
            uint32_t temp_addr = _this->next_addr() - _this->loc_base;
            _this->tcdm_req->init();
            _this->tcdm_req->set_addr(temp_addr);
            _this->tcdm_req->set_data(_this->access_buffer);

            /* For STOR_Z, process BEFORE sending (fills access_buffer) */
            if (_this->compute_able != 0 && (_this->iter_instruction == INSTR_STOR_Z))
                _this->process_iter_instruction();

            vp::IoReqStatus err = _this->send_tcdm_req();
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][ROUTINE ijk=%d-%d-%d] Send TCDM req #%d [addr=0x%08x]\n",
                _this->iter_i, _this->iter_j, _this->iter_k,
                _this->fsm_counter, temp_addr);

            if (err != vp::IO_REQ_OK) {
                _this->trace.fatal("[LightRedmule][ROUTINE] TCDM error\n");
                return;
            }

            /* For loads, process AFTER sending (reads access_buffer) */
            if (_this->compute_able != 0 && _this->iter_instruction != INSTR_STOR_Z)
                _this->process_iter_instruction();

            uint32_t receive_stamp = _this->tcdm_req->get_latency() + _this->fsm_timestamp;
            _this->pending_req_queue.push(receive_stamp);
            _this->fsm_counter += 1;
        }

        /* --- Drain completed responses --- */
        while ((_this->pending_req_queue.size() != 0) &&
               (_this->pending_req_queue.front() <= _this->fsm_timestamp)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][ROUTINE ijk=%d-%d-%d] Receive TCDM resp\n",
                _this->iter_i, _this->iter_j, _this->iter_k);
            _this->pending_req_queue.pop();
        }

        /* --- Transition when all blocks done --- */
        if ((_this->fsm_counter >= _this->tcdm_block_total) &&
            (_this->pending_req_queue.size() == 0))
        {
            int modeled_runtime = _this->get_redmule_array_runtime();
            int64_t latency = 1;

            /* Compensate for compute time vs. memory access time */
            if (_this->fsm_timestamp >= (uint32_t)modeled_runtime) {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "[LightRedmule][ROUTINE] TCDM time(%d) ≥ Model time(%d)\n",
                    _this->fsm_timestamp, modeled_runtime);
                latency = 1;
            } else {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "[LightRedmule][ROUTINE] Model time(%d) > TCDM time(%d)\n",
                    modeled_runtime, _this->fsm_timestamp);
                latency = modeled_runtime - _this->fsm_timestamp + 1;
            }

            _this->fsm_counter   = 0;
            _this->fsm_timestamp = 0;

            /* Forward Y→Z if this was the last k iteration */
            if ((_this->compute_able != 0) && (_this->iter_k + 1 == _this->x_row_tiles)) {
                _this->iter_instruction = INSTR_FORWARD_YZ;
                _this->process_iter_instruction();
            }

            if (_this->next_iteration() == 0) {
                /* More tiles to process */
                _this->tcdm_block_total = _this->get_routine_access_block_number();
                _this->state.set(ROUTINE);
            } else {
                /* All tiles done — store final Z tile */
                _this->tcdm_block_total = _this->get_storing_access_block_number();
                _this->state.set(STORING);
                latency += _this->get_routine_to_storing_latency();
            }

            _this->event_enqueue(_this->fsm_event, latency);
        } else {
            _this->state.set(ROUTINE);
            _this->event_enqueue(_this->fsm_event, 1);
        }
        break;
    }

    /* ================================================================
     *  STORING — Write the final Z tile to TCDM.
     * ================================================================ */
    case STORING: {
        if ((_this->fsm_counter < _this->tcdm_block_total) &&
            (_this->pending_req_queue.size() <= _this->queue_depth))
        {
            uint32_t temp_addr = _this->next_addr() - _this->loc_base;
            _this->tcdm_req->init();
            _this->tcdm_req->set_addr(temp_addr);
            _this->tcdm_req->set_data(_this->access_buffer);

            if (_this->compute_able != 0 && (_this->iter_instruction == INSTR_STOR_Z))
                _this->process_iter_instruction();

            vp::IoReqStatus err = _this->send_tcdm_req();
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Storing] Send TCDM req #%d [addr=0x%08x]\n",
                _this->fsm_counter, temp_addr);

            if (err != vp::IO_REQ_OK) {
                _this->trace.fatal("[LightRedmule][Storing] TCDM error\n");
                return;
            }

            uint32_t receive_stamp = _this->tcdm_req->get_latency() + _this->fsm_timestamp;
            _this->pending_req_queue.push(receive_stamp);
            _this->fsm_counter += 1;
        }

        while ((_this->pending_req_queue.size() != 0) &&
               (_this->pending_req_queue.front() <= _this->fsm_timestamp)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Storing] Receive TCDM resp\n");
            _this->pending_req_queue.pop();
        }

        if ((_this->fsm_counter >= _this->tcdm_block_total) &&
            (_this->pending_req_queue.size() == 0))
        {
            _this->tcdm_block_total = 0;
            _this->fsm_counter   = 0;
            _this->fsm_timestamp = 0;
            _this->state.set(FINISHED);
            _this->event_enqueue(_this->fsm_event, 1);

            /* Performance report */
            int64_t start_ns  = _this->timer_start / 1000;
            int64_t end_ns    = _this->time.get_time() / 1000;
            int64_t period_ns = end_ns - start_ns;
            int64_t period_clk = _this->clock.get_cycles() - _this->cycle_start;
            double  uti = (1.0 * _this->ideal_runtime) / (1.0 * period_clk);
            _this->total_runtime += period_ns;
            _this->num_matmul    += 1;

            _this->trace.msg(
                "[LightRedmule] Finished: %ld→%ld ns | %ld cyc | uti=%.3f | "
                "GEMM#%ld | fmt=%d | M-N-K=%d-%d-%d\n",
                start_ns, end_ns, period_clk, uti,
                _this->num_matmul, _this->compute_able,
                _this->m_size, _this->n_size, _this->k_size);
        } else {
            _this->state.set(STORING);
            _this->event_enqueue(_this->fsm_event, 1);
        }
        break;
    }

    /* ================================================================
     *  FINISHED — Fire IRQ or prepare to respond to stalled query.
     * ================================================================ */
    case FINISHED: {
        if (_this->redmule_query == NULL) {
            /* XIF mode or async MMIO: signal IRQ, go to IDLE */
            _this->done.sync(true);
            _this->state.set(IDLE);
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][FINISHED] IRQ fired, → IDLE\n");
        } else {
            /* Sync MMIO mode: need to respond to stalled core */
            _this->state.set(ACKNOWLEDGE);
        }
        _this->event_enqueue(_this->fsm_event, 1);
        break;
    }

    /* ================================================================
     *  ACKNOWLEDGE — Respond to the stalled MMIO read, then IDLE.
     * ================================================================ */
    case ACKNOWLEDGE: {
        _this->redmule_query->get_resp_port()->resp(_this->redmule_query);
        _this->redmule_query = NULL;
        _this->done.sync(true);
        _this->state.set(IDLE);
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "[LightRedmule][ACKNOWLEDGE] Core unstalled, → IDLE\n");
        _this->event_enqueue(_this->fsm_event, 1);
        break;
    }

    default:
        _this->trace.fatal("[LightRedmule] INVALID state: %d\n", _this->state);
    }
}

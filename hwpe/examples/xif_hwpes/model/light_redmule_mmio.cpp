/*
 * LightRedMulE — MMIO Register Handler
 * =======================================
 *
 * This file implements the traditional MMIO (memory-mapped I/O) interface
 * to LightRedMulE.  In Magia's XIF mode, the core uses mcnfig/marith
 * custom instructions instead (see light_redmule_offload.cpp).  But
 * LightRedMulE also supports MMIO for backward compatibility and for
 * simpler chips that don't have the eXtension Interface.
 *
 * Register map (byte offsets from HWPE base address):
 *
 *   Offset  R/W   Description
 *   ------  ----  -----------------------------------------------
 *   0x00    W     M_SIZE  — number of rows in X (and Z)
 *   0x04    W     N_SIZE  — inner dimension (cols of X = rows of W)
 *   0x08    W     K_SIZE  — number of cols in W (and Z)
 *   0x0C    W     X_ADDR  — TCDM address of input matrix X
 *   0x10    W     Y_ADDR  — TCDM address of bias matrix Y
 *   0x14    W     Z_ADDR  — TCDM address of output matrix Z
 *   0x18    W     W_ADDR  — TCDM address of weight matrix W
 *   0x20    R     TRIGGER (sync)  — core stalls until GEMM finishes
 *   0x24    R     TRIGGER (async) — core continues, use IRQ/poll
 *   0x28    R     WAIT           — stall until ongoing GEMM finishes
 *
 * When the core reads offset 0x20 and the FSM is IDLE, the model
 * starts computing and returns IO_REQ_PENDING.  The core is stalled
 * until the FSM reaches ACKNOWLEDGE and calls resp().
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#include "light_redmule.h"

vp::IoReqStatus LightRedmule::req(vp::Block *__this, vp::IoReq *req)
{
    LightRedmule *_this = (LightRedmule *)__this;

    uint64_t offset   = req->get_addr();
    uint8_t *data     = req->get_data();
    uint64_t size     = req->get_size();
    bool     is_write = req->get_is_write();

    /* ================================================================
     *  Synchronous Trigger (offset 32 = 0x20, read, IDLE)
     *  Core stalls until the GEMM finishes.
     * ================================================================ */
    if ((is_write == 0) && (offset == 32) &&
        (_this->redmule_query == NULL) && (_this->state.get() == IDLE))
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "[LightRedmule] Sync trigger: M=%d N=%d K=%d\n",
            _this->m_size, _this->n_size, _this->k_size);

        if ((_this->m_size == 0) || (_this->n_size == 0) || (_this->k_size == 0)) {
            _this->trace.fatal("[LightRedmule] INVALID config for sync trigger\n");
            return vp::IO_REQ_OK;
        }

        _this->init_redmule_meta_data();

        _this->state.set(PRELOAD);
        _this->tcdm_block_total = _this->get_preload_access_block_number();
        _this->fsm_counter   = 0;
        _this->fsm_timestamp = 0;
        _this->timer_start   = _this->time.get_time();
        _this->cycle_start   = _this->clock.get_cycles();
        _this->compute_able  = 0;
        _this->event_enqueue(_this->fsm_event, 1);

        /* Save the request — core stays stalled until we respond */
        _this->redmule_query = req;
        return vp::IO_REQ_PENDING;
    }

    /* ================================================================
     *  Asynchronous Trigger (offset 36 = 0x24, read, IDLE)
     *  Core continues immediately; use IRQ or poll.
     * ================================================================ */
    else if ((is_write == 0) && (offset == 36) && (_this->state.get() == IDLE))
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "[LightRedmule] Async trigger: M=%d N=%d K=%d\n",
            _this->m_size, _this->n_size, _this->k_size);

        if ((_this->m_size == 0) || (_this->n_size == 0) || (_this->k_size == 0)) {
            _this->trace.fatal("[LightRedmule] INVALID config for async trigger\n");
            return vp::IO_REQ_OK;
        }

        _this->init_redmule_meta_data();

        _this->state.set(PRELOAD);
        _this->tcdm_block_total = _this->get_preload_access_block_number();
        _this->fsm_counter   = 0;
        _this->fsm_timestamp = 0;
        _this->timer_start   = _this->time.get_time();
        _this->cycle_start   = _this->clock.get_cycles();
        _this->compute_able  = 0;
        _this->event_enqueue(_this->fsm_event, 1);
        /* No stall — return immediately */
    }

    /* ================================================================
     *  Asynchronous Wait (offset 40 = 0x28, read, not IDLE)
     *  Core stalls until current computation finishes.
     * ================================================================ */
    else if ((is_write == 0) && (offset == 40) &&
             (_this->redmule_query == NULL) && (_this->state.get() != IDLE))
    {
        _this->redmule_query = req;
        return vp::IO_REQ_PENDING;
    }

    /* ================================================================
     *  Register writes (offsets 0–28)
     * ================================================================ */
    else {
        uint32_t value = *(uint32_t *)data;

        switch (offset) {
            case 0:
                _this->m_size = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] M_SIZE = %d\n", value);
                break;
            case 4:
                _this->n_size = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] N_SIZE = %d\n", value);
                break;
            case 8:
                _this->k_size = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] K_SIZE = %d\n", value);
                break;
            case 12:
                _this->x_addr = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] X_ADDR = 0x%x\n", value);
                break;
            case 16:
                _this->y_addr = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] Y_ADDR = 0x%x\n", value);
                break;
            case 20:
                _this->z_addr = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] Z_ADDR = 0x%x\n", value);
                break;
            case 24:
                _this->w_addr = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] W_ADDR = 0x%x\n", value);
                break;
            case 32:
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] write to status (nop)\n");
                break;
            default:
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule] write to INVALID offset %d\n", (int)offset);
        }
    }

    return vp::IO_REQ_OK;
}

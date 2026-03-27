/*
 * Lemon Dot HWPE — GVSoC simulation model
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Cody Glassbrenner
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * This file implements the cycle-level C++ model that GVSoC loads
 * when the lemon_dot accelerator is enabled in the target config.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  How this models a "toy systolic array"                         │
 * ├─────────────────────────────────────────────────────────────────┤
 * │  A real systolic array (like in RedMulE or Google's TPU) has    │
 * │  a grid of processing elements (PEs) that each do one MAC       │
 * │  per cycle.  Data flows through the array in a pipeline.        │
 * │                                                                 │
 * │  Our toy models this as a multi-phase FSM:                      │
 * │                                                                 │
 * │   IDLE ──trigger──→ LOAD_A ──→ LOAD_B ──→ COMPUTE ──→ STORE_C   │
 * │     ↑                                                    │      │
 * │     └────────────────── IRQ ─────────────────────────────┘      │
 * │                                                                 │
 * │   LOAD_A  : Stream matrix A from L1 into internal buffer.       │
 * │             One element per cycle (models the "weight load"     │
 * │             phase of a weight-stationary dataflow).             │
 * │                                                                 │
 * │   LOAD_B  : Stream matrix B from L1 into internal buffer.       │
 * │             One element per cycle (models input activation      │
 * │             streaming).                                         │
 * │                                                                 │
 * │   COMPUTE : Compute one output element C[i][j] per cycle.       │
 * │             Each element is the dot product of row i of A       │
 * │             with column j of B:                                 │
 * │               C[i][j] = Σ_k A[i][k] * B[k][j]                   │
 * │             In a real systolic array, these K MACs happen       │
 * │             across K PEs in a pipeline — here we just model     │
 * │             the throughput (one result per cycle after          │
 * │             pipeline fill).                                     │
 * │                                                                 │
 * │   STORE_C : Write the result matrix back to L1, one element     │
 * │             per cycle.                                          │
 * │                                                                 │
 * │   Total cycles ≈ M*K + K*N + M*N + M*N                          │ 
 * │   For 4×4: 16 + 16 + 16 + 16 = 64 cycles                        │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * === File structure ===
 *
 * SECTION 1 — Includes & constants
 * SECTION 2 — Class definition (ports, buffers, FSM state)
 * SECTION 3 — Constructor & reset
 * SECTION 4 — Memory access helper
 * SECTION 5 — MMIO handler (hwpe_slave) — control + job registers
 * SECTION 6 — FSM handler  — the multi-phase computation engine
 * SECTION 7 — Factory function
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * SECTION 1 — Register offsets (must match archi_lemon_dot.h)        *
 * ------------------------------------------------------------------ */

/* Control registers */
#define LEMON_DOT_TRIGGER       0x00
#define LEMON_DOT_ACQUIRE       0x04
#define LEMON_DOT_FINISHED      0x08
#define LEMON_DOT_STATUS        0x0C
#define LEMON_DOT_RUNNING_JOB   0x10
#define LEMON_DOT_SOFT_CLEAR    0x14

/* Job registers (offsets relative to REG_OFFS) */
#define LEMON_DOT_REG_OFFS      0x40
#define LEMON_DOT_REG_A_PTR     0x00
#define LEMON_DOT_REG_B_PTR     0x04
#define LEMON_DOT_REG_C_PTR     0x08
#define LEMON_DOT_REG_M_SIZE    0x0C
#define LEMON_DOT_REG_K_SIZE    0x10
#define LEMON_DOT_REG_N_SIZE    0x14

/* Internal limits */
#define MAX_DIM  16

/* ------------------------------------------------------------------ *
 * SECTION 2 — Class definition                                       *
 * ------------------------------------------------------------------ */

class LemonDot : public vp::Component
{
public:
    LemonDot(vp::ComponentConf &config);

    void reset(bool active) override;

    static vp::IoReqStatus hwpe_slave(vp::Block *__this, vp::IoReq *req);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    bool access_mem(uint32_t addr, uint8_t *data, int size, bool is_write);

private:
    vp::Trace       trace;

    /* Ports — same pattern as lemon_adder */
    vp::IoSlave     in;              /* MMIO from CPU         */ // Recieves transaction requests from the CPU over the interconnect. CPU uses this to configure and read status of the HWPE.
    vp::IoMaster    out;             /* memory read/write L1  */ // Puts transaction requests to L1 memory on the interconnect, so outputs a *request* for a read/write, and will recieve/give data accordingly
    vp::WireMaster<bool> irq;        /* IRQ to event unit     */

    vp::IoReq       io_req;
    vp::ClockEvent *fsm_event;

    /* ---- Job registers (programmed by the CPU before TRIGGER) ---- */
    uint32_t reg_a_ptr;
    uint32_t reg_b_ptr;
    uint32_t reg_c_ptr;
    uint32_t reg_m;
    uint32_t reg_k;
    uint32_t reg_n;

    /* ---- Internal buffers (model the PE array's local storage) ---- */
    int32_t buf_a[MAX_DIM * MAX_DIM];   /* copy of matrix A  */
    int32_t buf_b[MAX_DIM * MAX_DIM];   /* copy of matrix B  */
    int32_t buf_c[MAX_DIM * MAX_DIM];   /* computed result C */

    /* ---- FSM state ---- */
    enum FsmState {
        IDLE,
        LOAD_A,     /* streaming A from L1 into buf_a */
        LOAD_B,     /* streaming B from L1 into buf_b */
        COMPUTE,    /* computing dot products → buf_c */
        STORE_C     /* writing buf_c back to L1       */
    } state;

    uint32_t idx;    /* element counter within the current phase */
    uint32_t cycle_count;  /* total cycles since TRIGGER — for tracing */
};

/* ------------------------------------------------------------------ *
 * SECTION 3 — Constructor & reset                                    *
 * ------------------------------------------------------------------ */

LemonDot::LemonDot(vp::ComponentConf &config) : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_master_port("out", &this->out);
    this->new_master_port("irq", &this->irq);

    this->in.set_req_meth(&LemonDot::hwpe_slave);
    this->new_slave_port("input", &this->in);

    this->fsm_event = this->event_new(&LemonDot::fsm_handler);

    this->trace.msg("LemonDot build complete\n");
}

void LemonDot::reset(bool active)
{
    if (active) {
        this->state   = IDLE;
        this->idx     = 0;
        this->cycle_count = 0;
        this->reg_a_ptr = 0;
        this->reg_b_ptr = 0;
        this->reg_c_ptr = 0;
        this->reg_m = 0;
        this->reg_k = 0;
        this->reg_n = 0;
        memset(this->buf_a, 0, sizeof(this->buf_a));
        memset(this->buf_b, 0, sizeof(this->buf_b));
        memset(this->buf_c, 0, sizeof(this->buf_c));
    }
}

/* ------------------------------------------------------------------ *
 * SECTION 4 — Memory access helper                                   *
 *                                                                    *
 * Reads or writes 'size' bytes at 'addr' through the master port.    *
 * This models the HWPE issuing load/store requests on the cluster    *
 * interconnect to access L1 TCDM — exactly like a real HWPE would.   *
 * ------------------------------------------------------------------ */

bool LemonDot::access_mem(uint32_t addr, uint8_t *data, int size, bool is_write)
{   // this helper is here to make the code more readable further down, since mem-accesses have similar patterns
    this->io_req.init();
    this->io_req.set_addr(addr);
    this->io_req.set_size(size);
    this->io_req.set_data(data);
    this->io_req.set_is_write(is_write);

    if (this->out.req(&this->io_req) != vp::IO_REQ_OK) { // catch failed request
        this->trace.fatal("Memory access FAILED at 0x%08x (%s, %d bytes)\n",
                          addr, is_write ? "write" : "read", size);
        return false;
    }
    return true; // otherwise request succeeded - return true
}

/* ------------------------------------------------------------------ *
 * SECTION 5 — MMIO handler                                           *
 *                                                                    *
 * Every time the RISC-V core does a load or store to our address     *
 * range (0x10201000–0x102013FF), GVSoC calls this function.          *
 *                                                                    *
 * Think of this as the decoder logic in the HWPE's register file:    *
 * it looks at the offset, figures out which register is being        *
 * accessed, and either latches a new value (write) or returns the    *
 * current value (read).                                              *
 * ------------------------------------------------------------------ */

vp::IoReqStatus LemonDot::hwpe_slave(vp::Block *__this, vp::IoReq *req)
{
    LemonDot *_this = (LemonDot *)__this;

    uint32_t  offset   = req->get_addr();
    bool      is_write = req->get_is_write();
    uint32_t *data     = (uint32_t *)req->get_data();

    if (is_write) {
        uint32_t value = *data;

        if (offset >= LEMON_DOT_REG_OFFS) {
            /* ---- Job registers ---- */
            uint32_t reg = offset - LEMON_DOT_REG_OFFS;
            switch (reg) {
                case LEMON_DOT_REG_A_PTR:
                    _this->trace.msg("REG  A_PTR  ← 0x%08x\n", value);
                    _this->reg_a_ptr = value;
                    break;
                case LEMON_DOT_REG_B_PTR:
                    _this->trace.msg("REG  B_PTR  ← 0x%08x\n", value);
                    _this->reg_b_ptr = value;
                    break;
                case LEMON_DOT_REG_C_PTR:
                    _this->trace.msg("REG  C_PTR  ← 0x%08x\n", value);
                    _this->reg_c_ptr = value;
                    break;
                case LEMON_DOT_REG_M_SIZE:
                    _this->trace.msg("REG  M_SIZE ← %d\n", value);
                    _this->reg_m = value;
                    break;
                case LEMON_DOT_REG_K_SIZE:
                    _this->trace.msg("REG  K_SIZE ← %d\n", value);
                    _this->reg_k = value;
                    break;
                case LEMON_DOT_REG_N_SIZE:
                    _this->trace.msg("REG  N_SIZE ← %d\n", value);
                    _this->reg_n = value;
                    break;
                default:
                    _this->trace.msg("REG  UNKNOWN(0x%02x) ← 0x%x\n", offset, value);
                    break;
            }
        } else {
            /* ---- Control registers ---- */
            switch (offset) {
                case LEMON_DOT_TRIGGER:
                    _this->trace.msg("===== TRIGGER — starting matrix multiply =====\n");
                    _this->trace.msg("  A(%d×%d) @ 0x%08x  ×  B(%d×%d) @ 0x%08x  →  C(%d×%d) @ 0x%08x\n",
                        _this->reg_m, _this->reg_k, _this->reg_a_ptr,
                        _this->reg_k, _this->reg_n, _this->reg_b_ptr,
                        _this->reg_m, _this->reg_n, _this->reg_c_ptr);
                    _this->state = LOAD_A;
                    _this->idx   = 0;
                    _this->cycle_count = 0;
                    memset(_this->buf_c, 0, sizeof(_this->buf_c));
                    /* Schedule the FSM to start on the next clock cycle */
                    _this->event_enqueue(_this->fsm_event, 1);
                    break;

                case LEMON_DOT_SOFT_CLEAR:
                    _this->trace.msg("SOFT_CLEAR — resetting\n");
                    _this->state = IDLE;
                    _this->idx   = 0;
                    break;

                /* Writes to read-only regs are silently ignored */
                case LEMON_DOT_ACQUIRE:     break;
                case LEMON_DOT_FINISHED:    break;
                case LEMON_DOT_STATUS:      break;
                case LEMON_DOT_RUNNING_JOB: break;

                default:
                    _this->trace.msg("CTRL UNKNOWN(0x%02x) ← 0x%x\n", offset, value);
                    break;
            }
        }
    } else {
        /* ---- Read path ---- */
        switch (offset) {
            case LEMON_DOT_ACQUIRE:
                *data = (_this->state == IDLE) ? 0 : 1;
                break;
            case LEMON_DOT_STATUS:
                *data = (_this->state != IDLE) ? 1 : 0;
                break;
            case LEMON_DOT_FINISHED:
                *data = (_this->state == IDLE) ? 1 : 0;
                break;
            default:
                *data = 0;
                break;
        }
    }

    return vp::IO_REQ_OK;
}

/* ------------------------------------------------------------------ *
 * SECTION 6 — FSM handler (the "systolic engine")                    *
 *                                                                    *
 * This is called once per clock cycle while the HWPE is active.      *
 * Each phase processes one element per cycle, modeling the streaming *
 * throughput of a real systolic array.                               *
 * ------------------------------------------------------------------ */

void LemonDot::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    LemonDot *_this = (LemonDot *)__this;
    _this->cycle_count++;

    uint32_t M = _this->reg_m;
    uint32_t K = _this->reg_k;
    uint32_t N = _this->reg_n;

    switch (_this->state)
    {
    /* -------------------------------------------------------------- *
     * LOAD_A — Read one element of A per cycle from L1 TCDM          *
     *                                                                *
     * Models the "weight loading" phase: in a weight-stationary      *
     * dataflow, you'd preload the weight matrix into the PE array    *
     * before streaming activations through.                          *
     * -------------------------------------------------------------- */
    case LOAD_A: {
        uint32_t addr = _this->reg_a_ptr + _this->idx * 4;
        int32_t  val;
        _this->access_mem(addr, (uint8_t *)&val, 4, false);
        _this->buf_a[_this->idx] = val;

        uint32_t row = _this->idx / K;
        uint32_t col = _this->idx % K;
        _this->trace.msg("[cyc %3d] LOAD_A[%d][%d] = %d  (from 0x%08x)\n",
                         _this->cycle_count, row, col, val, addr);

        _this->idx++;
        if (_this->idx >= M * K) { // once A is full, start loading B
            _this->trace.msg("[cyc %3d] LOAD_A complete (%d elements)\n",
                             _this->cycle_count, M * K);
            _this->state = LOAD_B;
            _this->idx = 0;
        }
        /* Re-enqueue for next cycle */
        _this->event_enqueue(_this->fsm_event, 1);
        break;
    }

    /* -------------------------------------------------------------- *
     * LOAD_B — Read one element of B per cycle from L1 TCDM          *
     *                                                                *
     * Models "activation streaming": the input data flows into the   *
     * array from one side while weights stay stationary.             *
     * -------------------------------------------------------------- */
    case LOAD_B: {
        uint32_t addr = _this->reg_b_ptr + _this->idx * 4;
        int32_t  val;
        _this->access_mem(addr, (uint8_t *)&val, 4, false);
        _this->buf_b[_this->idx] = val;

        uint32_t row = _this->idx / N;
        uint32_t col = _this->idx % N;
        _this->trace.msg("[cyc %3d] LOAD_B[%d][%d] = %d  (from 0x%08x)\n",
                         _this->cycle_count, row, col, val, addr);

        _this->idx++;
        if (_this->idx >= K * N) { // once B is full, begin compute
            _this->trace.msg("[cyc %3d] LOAD_B complete (%d elements)\n",
                             _this->cycle_count, K * N);
            _this->state = COMPUTE;
            _this->idx = 0;
        }
        _this->event_enqueue(_this->fsm_event, 1);
        break;
    }

    /* -------------------------------------------------------------- *
     * COMPUTE — One dot product per cycle                            *
     *                                                                *
     * Output element C[i][j] = Σ_k  A[i][k] * B[k][j]                *
     *                                                                *
     * In a real systolic array, the K multiplications happen across  *
     * K PEs in a pipelined fashion.  After the pipeline fills        *
     * (K cycles), you get one result per cycle.  Here we model       *
     * just the steady-state throughput: one result per cycle.        *
     * -------------------------------------------------------------- */
    case COMPUTE: {
        uint32_t i = _this->idx / N;   /* output row */
        uint32_t j = _this->idx % N;   /* output col */

        int32_t acc = 0;
        for (uint32_t k = 0; k < K; k++) {
            int32_t a_val = _this->buf_a[i * K + k];
            int32_t b_val = _this->buf_b[k * N + j];
            acc += a_val * b_val;
        }
        _this->buf_c[_this->idx] = acc;

        _this->trace.msg("[cyc %3d] COMPUTE C[%d][%d] = %d  (dot product of row %d · col %d)\n",
                         _this->cycle_count, i, j, acc, i, j);

        _this->idx++;
        if (_this->idx >= M * N) { // once compute is finished, begin moving C from the buffer to L1 TCDM (memory)
            _this->trace.msg("[cyc %3d] COMPUTE complete (%d elements)\n",
                             _this->cycle_count, M * N);
            _this->state = STORE_C;
            _this->idx = 0;
        }
        _this->event_enqueue(_this->fsm_event, 1);
        break;
    }

    /* -------------------------------------------------------------- *
     * STORE_C — Write one result element per cycle back to L1 TCDM   *
     * -------------------------------------------------------------- */
    case STORE_C: {
        uint32_t addr = _this->reg_c_ptr + _this->idx * 4;
        int32_t  val  = _this->buf_c[_this->idx];

        _this->access_mem(addr, (uint8_t *)&val, 4, true);

        uint32_t i = _this->idx / N;
        uint32_t j = _this->idx % N;
        _this->trace.msg("[cyc %3d] STORE_C[%d][%d] = %d  (to 0x%08x)\n",
                         _this->cycle_count, i, j, val, addr);

        _this->idx++;
        if (_this->idx >= M * N) {
            _this->trace.msg("[cyc %3d] ===== JOB DONE in %d cycles =====\n",
                             _this->cycle_count, _this->cycle_count);
            _this->state = IDLE;
            _this->irq.sync(true);   /* assert IRQ → wake up the core */
        } else {
            _this->event_enqueue(_this->fsm_event, 1);
        }
        break;
    }

    case IDLE:
    default:
        /* Should not happen — just stop */
        break;
    }
}

/* ------------------------------------------------------------------ *
 * SECTION 7 — Factory function (required by GVSoC)                   *
 * ------------------------------------------------------------------ */

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new LemonDot(config);
}

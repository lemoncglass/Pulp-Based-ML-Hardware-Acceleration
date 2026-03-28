/*
 * LightRedMulE — XIF Offload Handler
 * =====================================
 *
 * This file implements the eXtension Interface (XIF) side of LightRedMulE.
 * When the CV32E40X core encounters an unknown opcode, it packages the
 * instruction fields into an IssOffloadInsn struct and fires it over the
 * offload wire.  The XifDecoder routes RedMulE-specific opcodes here.
 *
 * Two custom instructions are handled:
 *
 *   mcnfig (opcode 0b0001011, RISC-V custom-0):
 *     • rs1 = {K_SIZE[31:16], M_SIZE[15:0]}
 *     • rs2 = N_SIZE
 *     • Always granted immediately (just a config write, no stall).
 *
 *   marith (opcode 0b0101011, RISC-V custom-1):
 *     • rs1 = X pointer (input activation address)
 *     • rs2 = W pointer (weight address)
 *     • rs3 = Y/Z pointer (bias / output address)
 *     • imm = {format[2:0], op_sel[2:0]}  (packed in arg_d)
 *     • Granted immediately, then triggers the FSM asynchronously.
 *     • The core is NOT stalled — it can poll status or wait for IRQ.
 *
 * Also contains:
 *   op_foramt_parser() — decodes the format/operation field from marith
 *   send_tcdm_req()    — thin wrapper to send a TCDM request
 *
 * Data flow:
 *   CV32E40X core  ─── offload wire ───►  XifDecoder
 *                                            │
 *                  ◄── grant wire ────       │ (routes by opcode)
 *                                            │
 *                                            ▼
 *                                      LightRedmule::offload_sync()
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#include "light_redmule.h"

/* ------------------------------------------------------------------ */
/*  op_foramt_parser()                                                */
/*  Decodes the combined format+operation field from the marith imm.  */
/*                                                                    */
/*  Returns:                                                          */
/*    1 = uint16, 2 = int16, 3 = fp16,                               */
/*    5 = uint8,  6 = int8,  7 = fp8e4m3                             */
/*                                                                    */
/*  Currently only GeMM (operation==1) with FP16 (format==1) or      */
/*  FP8 (format==0) are supported.                                   */
/* ------------------------------------------------------------------ */

uint32_t LightRedmule::op_foramt_parser(uint32_t op_format)
{
    uint32_t data_format = op_format & 0x7;
    uint32_t operation   = (op_format >> 3) & 0x7;

    if ((operation == 1) && (data_format == 1))
        return 3;   /* fp16 */
    else if ((operation == 1) && (data_format == 0))
        return 7;   /* fp8e4m3 */
    else {
        this->trace.fatal(
            "[LightRedmule] Selected wrong operation/format combination "
            "[op_format=%d data_format=%d operation=%d]\n",
            op_format, data_format, operation);
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  send_tcdm_req()                                                   */
/* ------------------------------------------------------------------ */

vp::IoReqStatus LightRedmule::send_tcdm_req()
{
    return this->tcdm_itf.req(this->tcdm_req);
}

/* ------------------------------------------------------------------ */
/*  offload_sync()                                                    */
/*  XIF callback — called by the XifDecoder when it receives a        */
/*  RedMulE opcode (0b0001011 or 0b0101011).                          */
/* ------------------------------------------------------------------ */

void LightRedmule::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    LightRedmule *_this = (LightRedmule *)__this;
    uint32_t opc = insn->opcode & 0x7F;

    switch (opc) {

        /* ============================================================
         *  mcnfig  (custom-0: 0b0001011)
         *  Sets the matrix dimensions M, N, K.
         *
         *  Encoding in ISS (see light_redmule_iss.hpp / mcnfig_exec):
         *    arg_a = rs1 = {K_SIZE[31:16], M_SIZE[15:0]}
         *    arg_b = rs2 = N_SIZE
         * ============================================================ */
        case 0b0001011:
        {
            if (_this->state.get() == IDLE) {
                insn->granted = true;  /* config only — never stalls the core */
                _this->m_size = insn->arg_a & 0xFFFF;
                _this->n_size = insn->arg_b;
                _this->k_size = (insn->arg_a) >> 16;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "[LightRedmule] mcnfig: M=%d, N=%d, K=%d\n",
                    _this->m_size, _this->n_size, _this->k_size);
            }
            break;
        }

        /* ============================================================
         *  marith  (custom-1: 0b0101011)
         *  Sets addresses, format, AND triggers the computation.
         *
         *  Encoding in ISS (see light_redmule_iss.hpp / marith_exec):
         *    arg_a = rs1 = X_addr
         *    arg_b = rs2 = W_addr
         *    arg_c = rs3 = Y_addr  (also used as Z_addr)
         *    arg_d = imm = {op_sel[5:3], format[2:0]}
         * ============================================================ */
        case 0b0101011:
        {
            if (_this->state.get() == IDLE) {
                insn->granted = true;  /* granted — core continues, we compute async */
                _this->x_addr = insn->arg_a;
                _this->w_addr = insn->arg_b;
                _this->y_addr = insn->arg_c;
                _this->z_addr = _this->y_addr;   /* Z overwrites Y in-place */
                _this->compute_able = _this->op_foramt_parser(insn->arg_d);
                _this->elem_size = (_this->compute_able < 4) ? 2 : 1;

                _this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "[LightRedmule] marith: X=0x%08x, W=0x%08x, Y=0x%08x, fmt=%d\n",
                    _this->x_addr, _this->w_addr, _this->y_addr, _this->compute_able);

                /* Sanity check */
                if ((_this->m_size == 0) || (_this->n_size == 0) || (_this->k_size == 0)) {
                    _this->trace.fatal("[LightRedmule] INVALID config (M=%d N=%d K=%d)\n",
                                       _this->m_size, _this->n_size, _this->k_size);
                }

                /* Initialize tiling metadata */
                _this->init_redmule_meta_data();

                /* Trigger the FSM */
                _this->state.set(PRELOAD);
                _this->tcdm_block_total = _this->get_preload_access_block_number();
                _this->fsm_counter   = 0;
                _this->fsm_timestamp = 0;
                _this->timer_start   = _this->time.get_time();
                _this->cycle_start   = _this->clock.get_cycles();
                _this->event_enqueue(_this->fsm_event, 1);
            }
            break;
        }
    }
}

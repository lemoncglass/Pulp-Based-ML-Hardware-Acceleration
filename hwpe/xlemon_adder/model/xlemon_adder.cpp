/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 University of Missouri - Kansas City
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * XLemon Adder HWPE — GVSoC simulation model (XIF-only)
 *
 * This file implements the cycle-accurate C++ model for an eXtension
 * Interface (XIF) only HWPE accelerator.  Unlike the MMIO-based
 * lemon_adder, this model has NO memory-mapped registers.  Instead,
 * the core communicates via a custom RISC-V instruction ("xladd")
 * that is fired over the XIF offload wire.
 *
 * === How this file works ===
 *
 * SECTION 1 — Includes
 *   Pull in the GVSoC component API and the XIF offload types
 *   (IssOffloadInsn / IssOffloadInsnGrant).
 *
 * SECTION 2 — Class definition
 *   Inherits from vp::Component.  Declares:
 *     - ports : XIF offload slave (receives custom insns from XifDecoder),
 *               XIF grant master (sends grant back through XifDecoder to core),
 *               memory master (read/write L1 TCDM),
 *               IRQ master (signals completion)
 *     - offload_sync : static callback for incoming XIF instructions
 *     - fsm_handler  : clock event callback for the computation
 *     - state        : IDLE / RUNNING
 *
 * SECTION 3 — Constructor
 *   Creates trace, registers ports, allocates clock event.
 *   NO MMIO slave port — everything comes through the offload wire.
 *
 * SECTION 4 — XIF offload handler (offload_sync)
 *   Called by the XifDecoder when our opcode arrives:
 *     opcode = 0b0101011 (custom-1)
 *     arg_a = rs1 = pointer to operand A
 *     arg_b = rs2 = pointer to operand B
 *     arg_c = rs3 = pointer to result
 *   Sets insn->granted = true, extracts pointers, kicks FSM.
 *
 * SECTION 5 — FSM / computation
 *   1. Read operand A and B from L1 memory
 *   2. Compute result = A + B
 *   3. Write result back to L1 memory
 *   4. Fire IRQ to signal completion
 *
 * SECTION 6 — Factory function
 *   Required by GVSoC's plugin loader.  Must be extern "C".
 *
 * === XIF data flow ===
 *
 *   CV32E40X core
 *        │  (encounters unknown opcode 0b0101011)
 *        │  (ISS handler packs rs1,rs2,rs3 into IssOffloadInsn)
 *        │  (fires iss->exec.offload_insn())
 *        ▼
 *   XifDecoder
 *        │  (sees opcode[6:0] == 0b0101011, routes to our slave port)
 *        ▼
 *   XLemonAdder::offload_sync()
 *        │  (sets granted=true, extracts pointers, schedules FSM)
 *        ▼
 *   XLemonAdder::fsm_handler()
 *        │  (reads A, reads B, computes A+B, writes result)
 *        │  (fires IRQ)
 *        ▼
 *   Core wakes up on IRQ
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* XIF offload types — IssOffloadInsn<T> and IssOffloadInsnGrant<T>
 * These are the structs that flow over the offload / grant wires
 * between the core ISS and coprocessors via the XifDecoder.
 *
 * IssOffloadInsn fields:
 *   .opcode  — full 32-bit instruction word
 *   .arg_a   — rs1 value (from core register file)
 *   .arg_b   — rs2 value
 *   .arg_c   — rs3 value (for 3-register formats)
 *   .arg_d   — unsigned immediate (if present)
 *   .granted — set to true by the coprocessor to accept the instruction
 *
 * IssOffloadInsnGrant fields:
 *   .result  — optional writeback value (written to rd in the core)
 */
#include <cpu/iss/include/offload.hpp>

/* ------------------------------------------------------------------ *
 * SECTION 2 — Class definition                                       *
 * ------------------------------------------------------------------ */

class XLemonAdder : public vp::Component
{
public:
    XLemonAdder(vp::ComponentConf &config);

    void reset(bool active) override;

    /* XIF offload handler — called by the XifDecoder when it routes
     * a custom instruction to us.  This replaces the MMIO hwpe_slave. */
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);

    /* FSM handler — called on clock events to perform the computation.
     * Same role as in the MMIO version, but triggered by XIF instead
     * of a TRIGGER register write. */
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    /* Helper — issue a 4-byte read or write through the memory master port */
    bool access_mem(uint32_t addr, uint8_t *data, bool is_write);

private:
    vp::Trace       trace;          // Debug/trace channel

    /* ---- XIF ports (replace the MMIO slave port) ---- */

    /* Offload slave — receives custom instructions from the XifDecoder.
     * The XifDecoder's master wire connects to this slave port.
     * Signature must match: wire<IssOffloadInsn<uint32_t>*> */
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf;

    /* Grant master — sends grant/result back through the XifDecoder
     * to the core.  When the core needs to be unstalled (e.g. after
     * a long computation), we'd sync a grant on this wire.
     * For our simple adder the instruction is granted immediately and
     * the core waits for IRQ, so this wire is mostly unused — but it
     * must still be wired for the XifDecoder grant path to work. */
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf;

    /* ---- Memory and IRQ ports (same as MMIO version) ---- */
    vp::IoMaster    out;            // Master port — read/write L1 memory
    vp::WireMaster<bool> irq;       // Master port — interrupt line

    vp::IoReq       io_req;         // Reusable request object for memory access
    vp::ClockEvent *fsm_event;      // Clock event for the computation FSM

    /* ---- Internal state ---- */
    uint32_t reg_a_ptr;             // Address of operand A in L1
    uint32_t reg_b_ptr;             // Address of operand B in L1
    uint32_t reg_res_ptr;           // Address where result is written

    enum { IDLE, RUNNING } state;
};

/* ------------------------------------------------------------------ *
 * SECTION 3 — Constructor                                            *
 *                                                                    *
 * Compare with the MMIO lemon_adder constructor:                     *
 *   MMIO version had:   in.set_req_meth(&hwpe_slave)                 *
 *                        new_slave_port("input", &in)                *
 *   XIF version has:    offload_itf.set_sync_meth(&offload_sync)     *
 *                        new_slave_port("offload", &offload_itf)     *
 *                        new_master_port("offload_grant", &...)      *
 *                                                                    *
 * The "input" MMIO slave port is completely gone.                    *
 * ------------------------------------------------------------------ */

XLemonAdder::XLemonAdder(vp::ComponentConf &config) : vp::Component(config)
{
    // 1. Debug trace channel
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // 2. Master port to L1 memory (name "out" must match o_OUT in xlemon_adder.py)
    this->new_master_port("out", &this->out);

    // 3. Master port for the interrupt line (name "irq" must match o_IRQ)
    this->new_master_port("irq", &this->irq);

    // 4. XIF offload slave — receives custom instructions from the XifDecoder.
    //    set_sync_meth registers our static callback.
    //    Port name "offload" must match i_OFFLOAD() in xlemon_adder.py.
    this->offload_itf.set_sync_meth(&XLemonAdder::offload_sync);
    this->new_slave_port("offload", &this->offload_itf, this);

    // 5. XIF grant master — sends grant/result back to the XifDecoder.
    //    Port name "offload_grant" must match o_OFFLOAD_GRANT() in xlemon_adder.py.
    this->new_master_port("offload_grant", &this->offload_grant_itf, this);

    // 6. Allocate the clock event for the computation FSM
    this->fsm_event = this->event_new(&XLemonAdder::fsm_handler);

    this->trace.msg("XLemonAdder (XIF-only) build complete\n");
}

/* ------------------------------------------------------------------ *
 * Reset                                                              *
 * ------------------------------------------------------------------ */

void XLemonAdder::reset(bool active)
{
    if (active)
    {
        this->state = IDLE;
        this->reg_a_ptr   = 0;
        this->reg_b_ptr   = 0;
        this->reg_res_ptr = 0;
    }
}

/* ------------------------------------------------------------------ *
 * Helper — issue a 4-byte memory access through the master port      *
 * ------------------------------------------------------------------ */

bool XLemonAdder::access_mem(uint32_t addr, uint8_t *data, bool is_write)
{
    this->io_req.init();
    this->io_req.set_addr(addr);
    this->io_req.set_size(4);
    this->io_req.set_data(data);
    this->io_req.set_is_write(is_write);

    if (this->out.req(&this->io_req) != vp::IO_REQ_OK)
    {
        this->trace.fatal("Memory access failed at 0x%x (%s)\n",
                          addr, is_write ? "write" : "read");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ *
 * SECTION 4 — XIF offload handler                                    *
 *                                                                    *
 * This is the XIF equivalent of the MMIO hwpe_slave handler.         *
 * Instead of receiving memory-mapped reads/writes, we receive a      *
 * decoded custom instruction with register operands pre-packed.      *
 *                                                                    *
 * The XifDecoder calls this method when it sees our opcode           *
 * (0b0101011 = custom-1).  The IssOffloadInsn struct contains:       *
 *   insn->opcode = full 32-bit instruction word                      *
 *   insn->arg_a  = rs1 = pointer to operand A                        *
 *   insn->arg_b  = rs2 = pointer to operand B                        *
 *   insn->arg_c  = rs3 = pointer to result                           *
 *   insn->granted = we set this to true to accept the instruction    *
 *                                                                    *
 * If we set granted=true, the core proceeds to its next instruction. *
 * If we leave granted=false, the core stalls until we grant later.   *
 * ------------------------------------------------------------------ */

void XLemonAdder::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    XLemonAdder *_this = (XLemonAdder *)__this;

    /* Extract opcode[6:0] to verify this is our instruction */
    uint32_t opc = insn->opcode & 0x7F;

    switch (opc) {

        /* ============================================================
         *  xladd instruction  (custom-1: 0b0101011)
         *
         *  Encoding:
         *    31   27 26 25 24  20 19   15 14  12 11   7 6      0
         *    ┌───────┬────┬──────┬───────┬──────┬──────┬────────┐
         *    │  rs3  │ 00 │ rs2  │  rs1  │ 000  │00000 │0101011 │
         *    └───────┴────┴──────┴───────┴──────┴──────┴────────┘
         *       │              │       │
         *       │              │       └── arg_a = A pointer
         *       │              └── arg_b = B pointer
         *       └── arg_c = result pointer
         *
         *  The ISS handler (xladd_exec in xlemon_adder_iss.hpp)
         *  packs the register values and fires them here.
         * ============================================================ */
        case 0b0101011:
        {
            if (_this->state == IDLE) {
                /* Accept the instruction — core continues to next insn */
                insn->granted = true;

                /* Extract operand pointers from the instruction fields */
                _this->reg_a_ptr   = insn->arg_a;  /* rs1 = A pointer */
                _this->reg_b_ptr   = insn->arg_b;  /* rs2 = B pointer */
                _this->reg_res_ptr = insn->arg_c;  /* rs3 = result pointer */

                _this->trace.msg(
                    "[XLemonAdder] xladd: A_ptr=0x%08x, B_ptr=0x%08x, RES_ptr=0x%08x\n",
                    _this->reg_a_ptr, _this->reg_b_ptr, _this->reg_res_ptr);

                /* Kick the FSM — computation starts next cycle */
                _this->state = RUNNING;
                _this->event_enqueue(_this->fsm_event, 1);

            } else {
                /* Busy — don't grant.  Core will stall via insn_stall()
                 * in the ISS handler until we become IDLE and the core
                 * retries the instruction. */
                _this->trace.msg("[XLemonAdder] xladd REJECTED — busy\n");
                /* insn->granted remains false (default) */
            }
            break;
        }

        default:
            _this->trace.fatal("[XLemonAdder] Unknown opcode 0x%02x\n", opc);
            break;
    }
}

/* ------------------------------------------------------------------ *
 * SECTION 5 — FSM / computation handler                              *
 *                                                                    *
 * This is identical to the MMIO version — the computation doesn't    *
 * care how it was triggered.  The only difference is that on         *
 * completion we just fire the IRQ (no pending MMIO request to        *
 * respond to, since XIF grants happen at offload time).              *
 * ------------------------------------------------------------------ */

void XLemonAdder::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    XLemonAdder *_this = (XLemonAdder *)__this;

    int32_t operand_a, operand_b, result;

    // ---- 1. Read operand A from L1 memory ----
    if (_this->access_mem(_this->reg_a_ptr, (uint8_t *)&operand_a, false)) {
        _this->trace.msg("Read operand A = %d from 0x%x\n", operand_a, _this->reg_a_ptr);
    }

    // ---- 2. Read operand B from L1 memory ----
    if (_this->access_mem(_this->reg_b_ptr, (uint8_t *)&operand_b, false)) {
        _this->trace.msg("Read operand B = %d from 0x%x\n", operand_b, _this->reg_b_ptr);
    }

    // ---- 3. Compute ---- //
    result = operand_a + operand_b;
    _this->trace.msg("Computed %d + %d = %d\n", operand_a, operand_b, result);

    // ---- 4. Write result back to L1 memory ----
    if (_this->access_mem(_this->reg_res_ptr, (uint8_t *)&result, true)) {
        _this->trace.msg("Wrote result %d to 0x%x\n", result, _this->reg_res_ptr);
    }

    // ---- 5. Signal completion via IRQ ----
    _this->state = IDLE;
    _this->irq.sync(true);
    _this->trace.msg("Job done — IRQ fired\n");
}

/* ------------------------------------------------------------------ *
 * SECTION 6 — Factory function (required by GVSoC)                   *
 * ------------------------------------------------------------------ */

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new XLemonAdder(config);
}

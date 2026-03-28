/*
 * LightRedMulE — ISS Instruction Handlers (Reference Copy)
 * ===========================================================
 *
 * These are the C++ functions that run INSIDE the ISS (Instruction Set
 * Simulator) — i.e., inside the CV32E40X core model — when it decodes
 * a mcnfig or marith instruction.
 *
 * They are NOT part of the LightRedmule accelerator model.  Instead,
 * they run on the "core side" and package the instruction's register
 * operands into an IssOffloadInsn struct, then fire it over the
 * offload wire to whatever is connected (the XifDecoder, which routes
 * it to LightRedmule).
 *
 * Flow for mcnfig:
 *   1. Core decodes opcode 0b0001011
 *   2. Core calls mcnfig_exec()
 *   3. mcnfig_exec() reads rs1, rs2 from the register file
 *   4. Packs them into IssOffloadInsn { .opcode, .arg_a=rs1, .arg_b=rs2 }
 *   5. Calls iss->exec.offload_insn(&insn) — this fires the offload wire
 *   6. XifDecoder receives it → routes to LightRedmule::offload_sync()
 *   7. LightRedmule sets granted=true → core continues to next instruction
 *
 * Flow for marith:
 *   1. Core decodes opcode 0b0101011
 *   2. Core calls marith_exec()
 *   3. marith_exec() reads rs1, rs2, rs3, imm
 *   4. Packs into IssOffloadInsn { .opcode, .arg_a=rs1, .arg_b=rs2,
 *                                   .arg_c=rs3, .arg_d=imm }
 *   5. Fires offload wire
 *   6. LightRedmule grants + starts FSM
 *   7. If NOT granted (busy), core stalls via insn_stall()
 *
 * Original file: gvsoc/core/models/cpu/iss/include/isa/light_redmule.hpp
 * Author: Lorenzo Zuolo (Chips-IT)
 */

#pragma once

#include "cpu/iss/include/iss_core.hpp"
#include "cpu/iss/include/isa_lib/int.h"
#include "cpu/iss/include/isa_lib/macros.h"

/* ------------------------------------------------------------------ */
/*  mcnfig_exec — handler for the mcnfig instruction                  */
/*                                                                    */
/*  Encoding: 0000000 rs2 rs1 000 rd 0001011  (custom-0, R-type)     */
/*                                                                    */
/*  Semantics:                                                        */
/*    arg_a = rs1 = {K_SIZE[31:16], M_SIZE[15:0]}                     */
/*    arg_b = rs2 = N_SIZE                                            */
/*                                                                    */
/*  The accelerator extracts M, N, K and stores them in config regs.  */
/*  Always granted immediately (just a configuration write).          */
/* ------------------------------------------------------------------ */

static inline iss_reg_t mcnfig_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    IssOffloadInsn<iss_reg_t> offload_insn = {
        .opcode = insn->opcode,
        .arg_a  = REG_GET(0),   /* rs1: {K[31:16], M[15:0]} */
        .arg_b  = REG_GET(1),   /* rs2: N                    */
    };
    iss->exec.offload_insn(&offload_insn);

    if (!offload_insn.granted) {
        iss->exec.stall_reg = REG_OUT(0);
        iss->exec.insn_stall();
    }
    return iss_insn_next(iss, insn, pc);
}

/* ------------------------------------------------------------------ */
/*  marith_exec — handler for the marith instruction                   */
/*                                                                    */
/*  Encoding: rs3 00 rs2 rs1 fmt imm[7:0] 0101011  (custom-1)       */
/*                                                                    */
/*  Semantics:                                                        */
/*    arg_a = rs1 = X_addr (input activation pointer)                 */
/*    arg_b = rs2 = W_addr (weight pointer)                           */
/*    arg_c = rs3 = Y_addr / Z_addr (bias / output pointer)          */
/*    arg_d = imm = {op_sel[5:3], format[2:0]}                       */
/*                                                                    */
/*  This instruction both configures AND triggers the accelerator.    */
/*  The core continues if granted, or stalls if the accelerator is    */
/*  busy (state != IDLE).                                             */
/* ------------------------------------------------------------------ */

static inline iss_reg_t marith_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    IssOffloadInsn<iss_reg_t> offload_insn = {
        .opcode = insn->opcode,
        .arg_a  = REG_GET(0),   /* rs1: X_addr */
        .arg_b  = REG_GET(1),   /* rs2: W_addr */
        .arg_c  = REG_GET(2),   /* rs3: Y_addr */
        .arg_d  = UIM_GET(0),   /* imm: {op_sel, format} */
    };
    iss->exec.offload_insn(&offload_insn);

    if (!offload_insn.granted) {
        iss->exec.stall_reg = REG_OUT(0);
        iss->exec.insn_stall();
    }
    return iss_insn_next(iss, insn, pc);
}

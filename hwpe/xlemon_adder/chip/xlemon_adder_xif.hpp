/*
 * XLemon Adder — ISS Handler
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 University of Missouri - Kansas City
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com>
 *         (heavily assisted by Claude Opus 4.6 agent)
 *
 * This file runs INSIDE the core ISS (Instruction Set Simulator).
 * When the ISS decoder matches the xladd instruction (opcode 0b0101011),
 * it calls xladd_exec().  This function:
 *
 *   1. Reads the register values for rs1, rs2, rs3 from the simulated
 *      register file (these are the A/B/result pointers).
 *   2. Packs them into an IssOffloadInsn struct.
 *   3. Fires the offload wire (iss->exec.offload_insn()).
 *   4. If the accelerator doesn't grant, stalls the pipeline.
 *
 * This is the same pattern used by Magia's mcnfig_exec/marith_exec
 * in light_redmule_iss.hpp.
 *
 * Placement: This file must be reachable via the -DCONFIG_ISS_CORE_DIR
 *            compiler flag set in the core definition (core.py).
 *            The ISS includes it as: <CONFIG_ISS_CORE_DIR/xlemon_adder_xif.hpp>
 */

#pragma once

#include "cpu/iss/include/iss_core.hpp"
#include "cpu/iss/include/isa_lib/int.h"
#include "cpu/iss/include/isa_lib/macros.h"

static inline iss_reg_t xladd_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    /* Pack register operands into the offload struct.
     * REG_GET(0) = rs1 value = pointer to operand A
     * REG_GET(1) = rs2 value = pointer to operand B
     * REG_GET(2) = rs3 value = pointer to result   */
    IssOffloadInsn<iss_reg_t> offload_insn = {
        .opcode = insn->opcode,
        .arg_a  = REG_GET(0),
        .arg_b  = REG_GET(1),
        .arg_c  = REG_GET(2),
    };

    /* Fire the offload wire — this reaches the XifDecoder (or
     * directly the accelerator if wired without a decoder). */
    iss->exec.offload_insn(&offload_insn);

    /* If the accelerator didn't grant immediately, stall the core.
     * The grant will arrive later via the offload_grant wire and
     * unstall the pipeline. */
    if (!offload_insn.granted)
    {
        iss->exec.stall_reg = REG_OUT(0);
        iss->exec.insn_stall();
    }

    return iss_insn_next(iss, insn, pc);
}

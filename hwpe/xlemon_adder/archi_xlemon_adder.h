/*
 * XLemon Adder HWPE — Architecture / Instruction Encoding
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 University of Missouri - Kansas City
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * Unlike the MMIO lemon_adder, this file does NOT define a memory-mapped
 * register layout.  Instead, it defines the custom RISC-V instruction
 * encoding used to communicate with the accelerator over the eXtension
 * Interface (XIF).
 *
 * === Instruction: xladd ===
 *
 * The xladd instruction uses the RISC-V "custom-1" opcode (0b0101011)
 * and passes three register operands to the accelerator:
 *
 *   31   27 26 25 24  20 19   15 14  12 11   7 6      0
 *   ┌───────┬────┬──────┬───────┬──────┬──────┬────────┐
 *   │  rs3  │ 00 │ rs2  │  rs1  │ 000  │00000 │0101011 │
 *   └───────┴────┴──────┴───────┴──────┴──────┴────────┘
 *      │              │       │
 *      │              │       └── rs1 = pointer to operand A
 *      │              └── rs2 = pointer to operand B
 *      └── rs3 = pointer to result buffer
 *
 * Execution semantics:
 *   1. Core fires xladd with A_ptr in rs1, B_ptr in rs2, RES_ptr in rs3.
 *   2. Core's ISS handler packs these into IssOffloadInsn and fires the
 *      offload wire.
 *   3. XifDecoder routes opcode 0b0101011 to the XLemonAdder slave port.
 *   4. XLemonAdder::offload_sync() receives the instruction, grants it,
 *      reads A and B from memory, computes A+B, writes the result, and
 *      fires an IRQ.
 *
 * There is NO separate configuration instruction — the simple xladd
 * carries all needed information (three pointers) in a single shot.
 * More complex accelerators (like RedMulE) would use a separate
 * "config" instruction (mcnfig) on custom-0 before the "trigger"
 * instruction (marith) on custom-1.
 *
 * === Comparison with MMIO lemon_adder ===
 *
 * MMIO lemon_adder required:
 *   1. Store A_ptr   to base+0x40
 *   2. Store B_ptr   to base+0x44
 *   3. Store RES_ptr to base+0x48
 *   4. Store 0       to base+0x00  (TRIGGER)
 *   5. Wait for IRQ
 *
 * XIF xlemon_adder requires:
 *   1. Load A_ptr, B_ptr, RES_ptr into registers
 *   2. Execute the xladd custom instruction
 *   3. Wait for IRQ
 *
 * The XIF path eliminates the 4 store instructions and the address
 * decoder wiring.  The core doesn't need to know the accelerator's
 * base address — it just fires an instruction.
 */

#ifndef __ARCHI_XLEMON_ADDER_H__
#define __ARCHI_XLEMON_ADDER_H__

/* ---- xladd instruction opcode (custom-1) ---- */
#define XLEMON_ADDER_OPCODE        0b0101011   /* 0x2B */

/* ---- Bit field positions within the 32-bit instruction word ---- */
#define XLEMON_ADDER_OPCODE_BITS   7           /* bits [6:0]  = opcode */
#define XLEMON_ADDER_RD_SHIFT      7           /* bits [11:7] = rd (unused, set to 0) */
#define XLEMON_ADDER_FUNCT3_SHIFT  12          /* bits [14:12]= funct3 (set to 000) */
#define XLEMON_ADDER_RS1_SHIFT     15          /* bits [19:15]= rs1 index */
#define XLEMON_ADDER_RS2_SHIFT     20          /* bits [24:20]= rs2 index */
#define XLEMON_ADDER_RS3_SHIFT     27          /* bits [31:27]= rs3 index */

/* ---- Event used by the HWPE to signal completion ---- */
#define XLEMON_ADDER_EVT0          12

/* ---- Register indices for the inline assembly ---- */
/* These are the RISC-V register numbers we use to pass pointers.
 * We use t0-t2 (registers 5-7) which are caller-saved temporaries. */
#define XLEMON_REG_T0  0b00101     /* t0 = x5 : A pointer */
#define XLEMON_REG_T1  0b00110     /* t1 = x6 : B pointer */
#define XLEMON_REG_T2  0b00111     /* t2 = x7 : result pointer */

#endif /* __ARCHI_XLEMON_ADDER_H__ */

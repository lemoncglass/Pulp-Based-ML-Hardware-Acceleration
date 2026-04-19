/*
 * XLemon Adder HWPE — Hardware Abstraction Layer (HAL) — XIF version
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 University of Missouri - Kansas City
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * === XIF vs MMIO HAL comparison ===
 *
 * The MMIO HAL (hal_lemon_adder.h) provided helpers like:
 *   hwpe_trigger_job()       → store to TRIGGER register
 *   lemon_adder_set_a(val)   → store to A_PTR register
 *   lemon_adder_set_b(val)   → store to B_PTR register
 *   lemon_adder_set_res_ptr  → store to RES_PTR register
 *
 * This XIF HAL replaces ALL of those with a single function:
 *   xlemon_adder_xladd(a_ptr, b_ptr, res_ptr)
 *     → loads pointers into t0/t1/t2
 *     → fires the xladd custom instruction via inline assembly
 *
 * The instruction does both configuration AND triggering in one shot.
 * No base address, no register map, no store instructions needed.
 *
 * === How the inline assembly works ===
 *
 * The .word directive emits a raw 32-bit instruction word.  We build
 * the xladd encoding by OR-ing the register indices and opcode:
 *
 *   .word (rs3 << 27) | (00 << 25) | (rs2 << 20) | (rs1 << 15) |
 *         (000 << 12) | (00000 << 7) | (0101011 << 0)
 *
 * Before emitting the instruction, we move our C pointers into the
 * physical registers (t0, t1, t2) using addi instructions.
 *
 * === Important note on runability ===
 *
 * This code requires a CV32E40X core with XIF support and a XifDecoder
 * that routes opcode 0b0101011 to the XLemonAdder.  It will NOT work
 * on pulp-open's CV32E40P (RI5CY) which has no XIF offload wire.
 * This is a reference implementation for learning.
 */

#ifndef __HAL_XLEMON_ADDER_H__
#define __HAL_XLEMON_ADDER_H__

#include "archi_xlemon_adder.h"

/*
 * xlemon_adder_xladd — fire the xladd custom instruction
 *
 * This single call replaces the entire MMIO sequence of:
 *   lemon_adder_set_a(a_ptr);
 *   lemon_adder_set_b(b_ptr);
 *   lemon_adder_set_res_ptr(res_ptr);
 *   hwpe_trigger_job();
 *
 * Parameters:
 *   a_ptr   — L1 address of operand A (uint32_t, passed in rs1 = t0)
 *   b_ptr   — L1 address of operand B (uint32_t, passed in rs2 = t1)
 *   res_ptr — L1 address to write the result (uint32_t, passed in rs3 = t2)
 */
static inline void xlemon_adder_xladd(unsigned int a_ptr,
                                       unsigned int b_ptr,
                                       unsigned int res_ptr)
{
    /* Step 1: Move our C variables into the temporary registers t0-t2.    cd /home/cody/workspace/Pulp-Based-ML-Hardware-Acceleration/hwpe/xlemon_adder && \
    export PYTHONPATH=/home/cody/workspace/Pulp-Based-ML-Hardware-Acceleration/gvsoc/install/python:/home/cody/workspace/Pulp-Based-ML-Hardware-Acceleration/gvsoc/core/models:/home/cody/workspace/Pulp-Based-ML-Hardware-Acceleration/gvsoc/gapy/bin && \
    python3 -c "
    import sys, os, json
    sys.path.insert(0, 'chip')
    from xlemon_target import Target
    import gvsoc.systree
    from gvsoc.systree import generated_components
    
    t = Target(None, 'top', parser=None, options=None)
    # Trigger build
    t.get_config()
    
    print(f'Total generated components: {len(generated_components)}')
    for name, info in generated_components.items():
        print(f'  COMP: {name}')
        print(f'    sources[0]: {info.sources[0]}')
        print(f'    #sources: {len(info.sources)}')
        print(f'    #cflags: {len(info.cflags)}')
        print()
    " 2>&1 | tail -30

    
     *
     * The asm "addi tN, %0, 0" copies the C variable (provided via
     * the "r" constraint) into the named register.  The compiler picks
     * whatever register it wants for %0 and the addi transfers it. */
    asm volatile ("addi t0, %0, 0" :: "r"(a_ptr));
    asm volatile ("addi t1, %0, 0" :: "r"(b_ptr));
    asm volatile ("addi t2, %0, 0" :: "r"(res_ptr));

    /* Step 2: Emit the xladd instruction as a raw .word.
     *
     * Encoding:
     *   bits [31:27] = rs3 index = t2 = 0b00111 (register x7)
     *   bits [26:25] = 00  (unused)
     *   bits [24:20] = rs2 index = t1 = 0b00110 (register x6)
     *   bits [19:15] = rs1 index = t0 = 0b00101 (register x5)
     *   bits [14:12] = funct3 = 000
     *   bits [11: 7] = rd = 00000 (no writeback)
     *   bits [ 6: 0] = opcode = 0b0101011 (custom-1)
     *
     * The assembled 32-bit word:
     *   0b 00111_00_00110_00101_000_00000_0101011
     *      rs3    -- rs2   rs1   f3  rd    opcode
     */
    asm volatile(
         ".word (0b00111   << 27) | \
                (0b00      << 25) | \
                (0b00110   << 20) | \
                (0b00101   << 15) | \
                (0b000     << 12) | \
                (0b00000   <<  7) | \
                (0b0101011 <<  0)   \n");

    /*
     * After this instruction executes:
     *   1. The core's ISS handler (xladd_exec) fires the offload wire
     *   2. XifDecoder routes it to XLemonAdder::offload_sync()
     *   3. offload_sync() sets granted=true → core proceeds past the .word
     *   4. The FSM computes A+B in the background
     *   5. The accelerator fires an IRQ when done
     *
     * The caller should wait for the IRQ before reading the result.
     */
}

#endif /* __HAL_XLEMON_ADDER_H__ */

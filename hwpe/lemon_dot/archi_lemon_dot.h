/*
 * Lemon Dot HWPE — Architecture / Register Map
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Cody Glassbrenner
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * This accelerator computes C = A × B (integer matrix multiply).
 * Each element C[i][j] is the dot product of row i of A with column j of B,
 * hence the name "lemon_dot."
 *
 * ┌──────────────────────────────────────────────────────────────────┐
 * │  How RISC-V talks to this accelerator (ISA-level view)           │
 * ├──────────────────────────────────────────────────────────────────┤
 * │  There are NO special opcodes for programming the HWPE.          │
 * │  The CPU communicates entirely through memory-mapped I/O (MMIO). │
 * │  Every register below lives at a fixed address in the cluster's  │
 * │  peripheral space (0x10201000 + offset).                         │
 * │                                                                  │
 * │  Writing a register = a regular `sw` (store-word) instruction.   │
 * │  Reading a register = a regular `lw` (load-word)  instruction.   │
 * │                                                                  │
 * │  In the disassembly (`make disasm`) you will see patterns like:  │
 * │                                                                  │
 * │    lui   a5, 0x10201        # upper 20 bits of HWPE base addr    │
 * │    sw    a0, 0x040(a5)      # write A_PTR  (0x10201000+0x40)     │
 * │    sw    a1, 0x044(a5)      # write B_PTR  (0x10201000+0x44)     │
 * │    sw    a2, 0x048(a5)      # write C_PTR  (0x10201000+0x48)     │
 * │    li    a0, 4                                                   │
 * │    sw    a0, 0x04c(a5)      # write M_SIZE (0x10201000+0x4C)     │
 * │    sw    a0, 0x050(a5)      # write K_SIZE (0x10201000+0x50)     │
 * │    sw    a0, 0x054(a5)      # write N_SIZE (0x10201000+0x54)     │
 * │    sw    zero, 0x000(a5)    # TRIGGER! (write 0 to offset 0)     │
 * │                                                                  │
 * │  After TRIGGER, the core sleeps with PULP's custom `p.elw`       │
 * │  (event-load-word) instruction — this is the one ISA extension   │
 * │  you'll see.  It atomically waits for the HWPE's IRQ event and   │
 * │  clears it, so the core uses zero power while waiting.           │
 * │                                                                  │
 * │  Compare this with RedMulE: same HWPE protocol, same MMIO        │
 * │  pattern, just more registers (for FP format, bias, etc.).       │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * |=======================================================================|
 * || Control registers (standard HWPE interface)                         ||
 * |=======================================================================|
 * || # reg |  offset  |  content                                         ||
 * ||-------+----------+--------------------------------------------------||
 * ||    0  |  0x0000  |  TRIGGER      – write 0 to start a job           ||
 * ||    1  |  0x0004  |  ACQUIRE      – read: 0 = free, nonzero = busy   ||
 * ||    2  |  0x0008  |  FINISHED     – event-enable mask                ||
 * ||    3  |  0x000C  |  STATUS       – 0 = idle, 1 = busy               ||
 * ||    4  |  0x0010  |  RUNNING_JOB  – id of running job                ||
 * ||    5  |  0x0014  |  SOFT_CLEAR   – write to reset state machine     ||
 * |=======================================================================|
 * || Job-dependent registers (matrix multiply configuration)             ||
 * |=======================================================================|
 * ||    0  |  0x0040  |  A_PTR  – L1 pointer to matrix A  (M × K)        ||
 * ||    1  |  0x0044  |  B_PTR  – L1 pointer to matrix B  (K × N)        ||
 * ||    2  |  0x0048  |  C_PTR  – L1 pointer to output C  (M × N)        ||
 * ||    3  |  0x004C  |  M_SIZE – number of rows in A (and C)            ||
 * ||    4  |  0x0050  |  K_SIZE – inner dimension (cols A / rows B)      ||
 * ||    5  |  0x0054  |  N_SIZE – number of cols in B (and C)            ||
 * |=======================================================================|
 *
 * Memory layout (row-major, int32_t elements):
 *   A[i][k] lives at  A_PTR + (i * K + k) * 4
 *   B[k][j] lives at  B_PTR + (k * N + j) * 4
 *   C[i][j] lives at  C_PTR + (i * N + j) * 4
 */

#ifndef __ARCHI_LEMON_DOT_H__
#define __ARCHI_LEMON_DOT_H__

/* ---- Base address (cluster accelerator peripheral space) ---- */
#define LEMON_DOT_BASE_ADD      0x10201000

/* ---- Standard HWPE control register offsets ---- */
#define LEMON_DOT_TRIGGER       0x00
#define LEMON_DOT_ACQUIRE       0x04
#define LEMON_DOT_FINISHED      0x08
#define LEMON_DOT_STATUS        0x0C
#define LEMON_DOT_RUNNING_JOB   0x10
#define LEMON_DOT_SOFT_CLEAR    0x14

/* ---- Job-specific register offsets (relative to REG_OFFS) ---- */
#define LEMON_DOT_REG_OFFS      0x40
#define LEMON_DOT_REG_A_PTR     0x00   /* absolute: 0x40 */
#define LEMON_DOT_REG_B_PTR     0x04   /* absolute: 0x44 */
#define LEMON_DOT_REG_C_PTR     0x08   /* absolute: 0x48 */
#define LEMON_DOT_REG_M_SIZE    0x0C   /* absolute: 0x4C */
#define LEMON_DOT_REG_K_SIZE    0x10   /* absolute: 0x50 */
#define LEMON_DOT_REG_N_SIZE    0x14   /* absolute: 0x54 */

/* ---- Event used by the HWPE to signal completion ---- */
#define LEMON_DOT_EVT0          12

/* ---- Maximum supported dimension (per axis) ---- */
#define LEMON_DOT_MAX_DIM       16

#endif /* __ARCHI_LEMON_DOT_H__ */

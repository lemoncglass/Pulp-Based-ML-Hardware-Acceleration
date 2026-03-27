/*
 * Lemon Dot HWPE — Hardware Abstraction Layer (HAL)
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Cody Glassbrenner
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * Thin inline helpers that the test program calls instead of writing
 * raw addresses.  Each function compiles down to a single `sw`/`lw`
 * instruction — check the disassembly with `make disasm` to verify!
 *
 * The volatile qualifier is critical: it tells the compiler "this memory
 * location has side effects — do NOT optimise it away, reorder it, or
 * cache it in a register."  Without volatile, the compiler could (and
 * would) eliminate or reorder our MMIO writes.
 */

#ifndef __HAL_LEMON_DOT_H__
#define __HAL_LEMON_DOT_H__

#include "archi_lemon_dot.h"

/* ------------------------------------------------------------------ *
 * Low-level MMIO primitives                                          *
 *                                                                    *
 * These compile to exactly one `sw` (store word) or `lw` (load word) *
 * instruction plus the address setup.  Run `make disasm` and search  *
 * for "10201" to find every HWPE register access in the binary.      *
 * ------------------------------------------------------------------ */
#define HWPE_WRITE(value, offset) \
    (*(volatile int *)(LEMON_DOT_BASE_ADD + (offset)) = (value))

#define HWPE_READ(offset) \
    (*(volatile int *)(LEMON_DOT_BASE_ADD + (offset)))

/* ---- Standard HWPE control helpers ---- */

static inline void hwpe_trigger_job(void) {
    HWPE_WRITE(0, LEMON_DOT_TRIGGER);
}

static inline int hwpe_acquire_job(void) {
    return HWPE_READ(LEMON_DOT_ACQUIRE);
}

static inline unsigned int hwpe_get_status(void) {
    return HWPE_READ(LEMON_DOT_STATUS);
}

static inline void hwpe_soft_clear(void) {
    HWPE_WRITE(0, LEMON_DOT_SOFT_CLEAR);
}

/* ---- Job-specific helpers: matrix pointers ---- */

static inline void lemon_dot_set_a_ptr(unsigned int addr) {
    HWPE_WRITE(addr, LEMON_DOT_REG_OFFS + LEMON_DOT_REG_A_PTR);
}

static inline void lemon_dot_set_b_ptr(unsigned int addr) {
    HWPE_WRITE(addr, LEMON_DOT_REG_OFFS + LEMON_DOT_REG_B_PTR);
}

static inline void lemon_dot_set_c_ptr(unsigned int addr) {
    HWPE_WRITE(addr, LEMON_DOT_REG_OFFS + LEMON_DOT_REG_C_PTR);
}

/* ---- Job-specific helpers: matrix dimensions ---- */

static inline void lemon_dot_set_m(unsigned int m) {
    HWPE_WRITE(m, LEMON_DOT_REG_OFFS + LEMON_DOT_REG_M_SIZE);
}

static inline void lemon_dot_set_k(unsigned int k) {
    HWPE_WRITE(k, LEMON_DOT_REG_OFFS + LEMON_DOT_REG_K_SIZE);
}

static inline void lemon_dot_set_n(unsigned int n) {
    HWPE_WRITE(n, LEMON_DOT_REG_OFFS + LEMON_DOT_REG_N_SIZE);
}

#endif /* __HAL_LEMON_DOT_H__ */

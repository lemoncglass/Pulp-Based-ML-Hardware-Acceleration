/*
 * Lemon Adder HWPE — Hardware Abstraction Layer (HAL)
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Cody Glassbrenner
 * 
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 * 
 * Thin inline helpers that the test program calls instead of writing raw
 * addresses.  This follows exactly the same pattern as hal_redmule.h.
 * 
 */

#ifndef __HAL_LEMON_ADDER_H__
#define __HAL_LEMON_ADDER_H__

#include "archi_lemon_adder.h"

/* ---- Low-level read / write ---- */
#define HWPE_WRITE(value, offset)  *(volatile int *)(LEMON_ADDER_BASE_ADD + (offset)) = (value)
#define HWPE_READ(offset)          *(volatile int *)(LEMON_ADDER_BASE_ADD + (offset))

/* ---- Control helpers ---- */
static inline void hwpe_trigger_job(void) {
    HWPE_WRITE(0, LEMON_ADDER_TRIGGER);
}

static inline int hwpe_acquire_job(void) {
    return HWPE_READ(LEMON_ADDER_ACQUIRE);
}

static inline unsigned int hwpe_get_status(void) {
    return HWPE_READ(LEMON_ADDER_STATUS);
}

static inline void hwpe_soft_clear(void) {
    HWPE_WRITE(0, LEMON_ADDER_SOFT_CLEAR);
}

/* ---- Job-specific helpers (REG_OFFS baked into the pointer addresses)---- */
static inline void lemon_adder_set_a(unsigned int value) {
    HWPE_WRITE(value, LEMON_ADDER_REG_OFFS + LEMON_ADDER_REG_A_PTR);
}

static inline void lemon_adder_set_b(unsigned int value) {
    HWPE_WRITE(value, LEMON_ADDER_REG_OFFS + LEMON_ADDER_REG_B_PTR);
}

static inline void lemon_adder_set_res_ptr(unsigned int value) {
    HWPE_WRITE(value, LEMON_ADDER_REG_OFFS + LEMON_ADDER_REG_RES_PTR);
}

#endif /* __HAL_LEMON_ADDER_H__ */

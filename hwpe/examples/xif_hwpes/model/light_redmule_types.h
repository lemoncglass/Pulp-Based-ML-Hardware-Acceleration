/*
 * LightRedMulE — Type Definitions
 * ================================
 *
 * This header defines the shared types, enums, and typedefs used throughout
 * the LightRedMulE GVSoC model.  Keeping them in one place avoids circular
 * dependencies when the model is split across multiple translation units.
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 * Authors: Chi Zhang (ETH Zurich), Lorenzo Zuolo (Chips-IT), Alex Marchioni (Chips-IT)
 */

#pragma once

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Floating-point typedefs                                           */
/* ------------------------------------------------------------------ */

typedef uint8_t  fp8e4m3;   /* E4M3 mini-float  (1-4-3 sign/exp/mantissa)  */
typedef uint16_t fp16;       /* IEEE 754 half-precision (1-5-10)             */

/* ------------------------------------------------------------------ */
/*  RedMulE FSM states                                                */
/*                                                                    */
/*  IDLE       → waiting for a trigger (MMIO read or XIF marith)      */
/*  PRELOAD    → load initial X and Y tiles from TCDM                 */
/*  ROUTINE    → main loop: load W tile, compute, overlap store/load  */
/*  STORING    → store the last Z tile after all iterations           */
/*  FINISHED   → computation done, fire IRQ or grant pending query    */
/*  ACKNOWLEDGE→ respond to a stalled MMIO synchronous trigger        */
/* ------------------------------------------------------------------ */

enum redmule_state {
    IDLE,
    PRELOAD,
    ROUTINE,
    STORING,
    FINISHED,
    ACKNOWLEDGE
};

/* ------------------------------------------------------------------ */
/*  Per-iteration micro-instructions                                  */
/*                                                                    */
/*  Within each ROUTINE iteration the FSM steps through these         */
/*  micro-ops to load/store the right buffers in the right order.     */
/* ------------------------------------------------------------------ */

enum iter_instruction {
    INSTR_LOAD_Y,           /* Load a row of Y (bias / accumulator) tile */
    INSTR_LOAD_W,           /* Load a row of W (weight) tile             */
    INSTR_LOAD_W_COMPUTE,   /* Load W row AND run compute on buffered data */
    INSTR_LOAD_X,           /* Load a row of X (input activation) tile   */
    INSTR_STOR_Z,           /* Store a row of Z (output) tile            */
    INSTR_FORWARD_YZ        /* Copy Y_preload → Z_compute buffer (new accumulation starts) */
};

/*
 * LightRedMulE — Matrix-Multiply Kernel Declarations
 * ====================================================
 *
 * Pure C functions that implement Z = Y + X · W for every supported data
 * format.  Each kernel is a straightforward triple-nested loop — no tiling,
 * no SIMD.  The GVSoC model calls the appropriate one depending on the
 * format field decoded from the marith instruction.
 *
 * Supported formats:
 *   compute_able = 1  →  matmul_uint16
 *   compute_able = 2  →  matmul_int16
 *   compute_able = 3  →  matmul_fp16   (uses FlexFloat FMA)
 *   compute_able = 5  →  matmul_uint8
 *   compute_able = 6  →  matmul_int8
 *   compute_able = 7  →  matmul_fp8e4m3 (naive float-domain FMA)
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#pragma once

#include "light_redmule_types.h"

/* ------------------------------------------------------------------ */
/*  Integer / unsigned kernels                                        */
/* ------------------------------------------------------------------ */

void matmul_uint16(uint16_t *z, uint16_t *y, uint16_t *x, uint16_t *w,
                   uint16_t m_size, uint16_t n_size, uint16_t k_size);

void matmul_int16 (int16_t  *z, int16_t  *y, int16_t  *x, int16_t  *w,
                   uint16_t m_size, uint16_t n_size, uint16_t k_size);

void matmul_uint8 (uint8_t  *z, uint8_t  *y, uint8_t  *x, uint8_t  *w,
                   uint16_t m_size, uint16_t n_size, uint16_t k_size);

void matmul_int8  (int8_t   *z, int8_t   *y, int8_t   *x, int8_t   *w,
                   uint16_t m_size, uint16_t n_size, uint16_t k_size);

/* ------------------------------------------------------------------ */
/*  Floating-point kernels                                            */
/* ------------------------------------------------------------------ */

void matmul_fp16    (fp16     *z, fp16     *y, fp16     *x, fp16     *w,
                     uint16_t m_size, uint16_t n_size, uint16_t k_size);

void matmul_fp8e4m3 (fp8e4m3  *z, fp8e4m3  *y, fp8e4m3  *x, fp8e4m3  *w,
                     uint16_t m_size, uint16_t n_size, uint16_t k_size);

/* ------------------------------------------------------------------ */
/*  FP8 E4M3 helper conversions                                      */
/* ------------------------------------------------------------------ */

float    fp8e4m3_to_float(fp8e4m3 value);
fp8e4m3  float_to_fp8e4m3(float value);
fp8e4m3  fp8e4m3_fma(fp8e4m3 a, fp8e4m3 b, fp8e4m3 c);

/* ------------------------------------------------------------------ */
/*  FP16 FMA (implemented via FlexFloat library)                      */
/* ------------------------------------------------------------------ */

fp16 fp16_fma(fp16 a, fp16 b, fp16 c);

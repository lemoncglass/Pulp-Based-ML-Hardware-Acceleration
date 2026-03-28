/*
 * LightRedMulE — Matrix-Multiply Kernels
 * =========================================
 *
 * Pure C implementations of Z = Y + X · W for each supported data format.
 * These are the "functional golden models" — they produce bit-exact results
 * that the RTL and FPGA implementations must match.
 *
 * All kernels follow the same triple-loop pattern:
 *   for each (i, j): z[i][j] = y[i][j]
 *   for each (i, j, k): z[i][j] += x[i][k] * w[k][j]
 *
 * For floating-point formats, fused multiply-add (FMA) is used to match
 * hardware behavior (single rounding instead of multiply-then-add).
 *
 * The FP16 FMA uses the FlexFloat library (part of GVSoC) for IEEE-754
 * compliant half-precision arithmetic.  The FP8-E4M3 FMA uses a naive
 * float-domain implementation (convert→compute→convert).
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#include "matmul_kernels.h"

#include <cmath>
#include <cstring>
#include <cpu/iss/flexfloat/flexfloat.h>

/* ==================================================================
 *  Integer Kernels
 * ================================================================== */

void matmul_uint16(uint16_t *z, uint16_t *y, uint16_t *x, uint16_t *w,
                   uint16_t m_size, uint16_t n_size, uint16_t k_size)
{
    for (int i = 0; i < m_size; ++i)
        for (int j = 0; j < k_size; ++j) {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
        }
}

void matmul_int16(int16_t *z, int16_t *y, int16_t *x, int16_t *w,
                  uint16_t m_size, uint16_t n_size, uint16_t k_size)
{
    for (int i = 0; i < m_size; ++i)
        for (int j = 0; j < k_size; ++j) {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
        }
}

void matmul_uint8(uint8_t *z, uint8_t *y, uint8_t *x, uint8_t *w,
                  uint16_t m_size, uint16_t n_size, uint16_t k_size)
{
    for (int i = 0; i < m_size; ++i)
        for (int j = 0; j < k_size; ++j) {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
        }
}

void matmul_int8(int8_t *z, int8_t *y, int8_t *x, int8_t *w,
                 uint16_t m_size, uint16_t n_size, uint16_t k_size)
{
    for (int i = 0; i < m_size; ++i)
        for (int j = 0; j < k_size; ++j) {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
        }
}

/* ==================================================================
 *  FP16 Kernel (uses FlexFloat library for IEEE-754 compliance)
 * ================================================================== */

fp16 fp16_fma(fp16 a, fp16 b, fp16 c)
{
    flexfloat_t ff_a, ff_b, ff_c, ff_res;
    flexfloat_desc_t env = (flexfloat_desc_t){5, 10};  /* 5-bit exp, 10-bit mantissa = FP16 */
    ff_init(&ff_a, env);
    ff_init(&ff_b, env);
    ff_init(&ff_c, env);
    ff_init(&ff_res, env);
    flexfloat_set_bits(&ff_a, a);
    flexfloat_set_bits(&ff_b, b);
    flexfloat_set_bits(&ff_c, c);
    ff_fma(&ff_res, &ff_a, &ff_b, &ff_c);
    return (fp16)flexfloat_get_bits(&ff_res);
}

void matmul_fp16(fp16 *z, fp16 *y, fp16 *x, fp16 *w,
                 uint16_t m_size, uint16_t n_size, uint16_t k_size)
{
    for (int i = 0; i < m_size; ++i)
        for (int j = 0; j < k_size; ++j) {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
                z[i * k_size + j] = fp16_fma(x[i * n_size + k],
                                              w[k * k_size + j],
                                              z[i * k_size + j]);
        }
}

/* ==================================================================
 *  FP8-E4M3 Kernel (naive float-domain conversion)
 *
 *  E4M3 format: 1 sign, 4 exponent, 3 mantissa bits
 *  Bias = 7
 * ================================================================== */

#define FP8_EXP_MASK  0x78  /* 0111 1000 */
#define FP8_FRAC_MASK 0x07  /* 0000 0111 */
#define FP8_SIGN_MASK 0x80  /* 1000 0000 */
#define FP8_BIAS      7

float fp8e4m3_to_float(fp8e4m3 value)
{
    uint8_t sign     = (value & FP8_SIGN_MASK) >> 7;
    uint8_t exponent = (value & FP8_EXP_MASK) >> 3;
    uint8_t fraction = value & FP8_FRAC_MASK;

    if (exponent == 0) {
        if (fraction == 0) return sign ? -0.0f : 0.0f;
        return (sign ? -1.0f : 1.0f) * (fraction / 8.0f) * powf(2, -6);
    } else if (exponent == 15) {
        return fraction ? NAN : (sign ? -INFINITY : INFINITY);
    }

    float mantissa = 1.0f + (fraction / 8.0f);
    float result   = mantissa * powf(2, exponent - FP8_BIAS);
    return sign ? -result : result;
}

fp8e4m3 float_to_fp8e4m3(float value)
{
    if (isnan(value))  return 0x7F;
    if (isinf(value))  return value < 0 ? 0xF8 : 0x78;

    uint8_t sign = (value < 0) ? 0x80 : 0x00;
    value = fabsf(value);

    int exponent;
    float mantissa = frexpf(value, &exponent);

    if (value == 0.0f) return 0;

    exponent += FP8_BIAS - 1;

    if (exponent < 1) {
        int frac = (int)roundf(value / powf(2, -6) * 8.0f);
        return sign | (frac & FP8_FRAC_MASK);
    } else if (exponent > 14) {
        return sign | 0x78;
    }

    uint8_t frac = (uint8_t)roundf((mantissa - 0.5f) * 16.0f);
    return sign | ((exponent << 3) & FP8_EXP_MASK) | (frac & FP8_FRAC_MASK);
}

fp8e4m3 fp8e4m3_fma(fp8e4m3 a, fp8e4m3 b, fp8e4m3 c)
{
    float fa = fp8e4m3_to_float(a);
    float fb = fp8e4m3_to_float(b);
    float fc = fp8e4m3_to_float(c);
    return float_to_fp8e4m3(fa * fb + fc);
}

void matmul_fp8e4m3(fp8e4m3 *z, fp8e4m3 *y, fp8e4m3 *x, fp8e4m3 *w,
                    uint16_t m_size, uint16_t n_size, uint16_t k_size)
{
    for (int i = 0; i < m_size; ++i)
        for (int j = 0; j < k_size; ++j) {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
                z[i * k_size + j] = fp8e4m3_fma(x[i * n_size + k],
                                                  w[k * k_size + j],
                                                  z[i * k_size + j]);
        }
}

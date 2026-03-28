/*
 * Golden (expected) output for the lemon_dot HWPE.
 *
 * C = A × B, M × N = 4 × 4, stored row-major.
 *
 * Hand-computed (verify with gen_stimuli.py):
 *
 *     |  9   4   7  10 |
 * C = | 21  16  19  22 |
 *     | 33  28  31  34 |
 *     | 45  40  43  46 |
 *
 * Example: C[0][0] = 1*1 + 2*0 + 3*0 + 4*2 = 1 + 0 + 0 + 8 = 9
 *          C[1][2] = 5*0 + 6*2 + 7*1 + 8*0 = 0 + 12 + 7 + 0 = 19
 */

#ifndef __GOLDEN_H__
#define __GOLDEN_H__

#define GOLDEN_DATA  { \
     9,  4,  7, 10,   \
    21, 16, 19, 22,    \
    33, 28, 31, 34,    \
    45, 40, 43, 46     \
}

#endif /* __GOLDEN_H__ */

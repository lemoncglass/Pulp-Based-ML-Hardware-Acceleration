#!/usr/bin/env python3
"""
Generate FP16 test vectors for RedMulE GEMM test.

RedMulE operates on IEEE 754 half-precision floats (FP16).
The uint16_t values in the C headers are raw FP16 bit patterns,
NOT integer values. We must:
  1. Choose float values for X and W
  2. Store them as FP16 bit patterns (uint16)
  3. Compute Z = X @ W in FP16 arithmetic
  4. Store Z as FP16 bit patterns

FP16 range: max ~65504, min subnormal ~5.96e-8
To avoid overflow in the dot product (summing N_SIZE=64 products),
we use small values so the accumulation stays within FP16 range.

- Claude Opus 4.6
"""

import numpy as np
import struct
import os

# ---- Relative Paths for flexibility ----
# GEMM_DIR = '.' ## Absolute path is too brittle, so use relative path from this script
TEST_DIR = '.' #os.path.join(GEMM_DIR, 'inc', 'test_headers')
os.makedirs(TEST_DIR, exist_ok=True)

# Dimensions from tensor_dim.h
M_SIZE = 48
N_SIZE = 64
K_SIZE = 48

# --- Generate X (M_SIZE x N_SIZE = 48 x 64 = 3072 elements) ---
# Use small sequential float values: 0.0, 0.01, 0.02, ...
# Max value in X: 3071 * 0.01 = 30.71 (fits FP16 easily)
x_floats = np.arange(M_SIZE * N_SIZE, dtype=np.float32) * 0.01
x_fp16 = x_floats.astype(np.float16)
x_bits = np.frombuffer(x_fp16.tobytes(), dtype=np.uint16)

# --- Generate W (N_SIZE x K_SIZE = 64 x 48 = 3072 elements) ---
# Use small sequential float values: 0.0, 0.01, 0.02, ...
w_floats = np.arange(N_SIZE * K_SIZE, dtype=np.float32) * 0.01
w_fp16 = w_floats.astype(np.float16)
w_bits = np.frombuffer(w_fp16.tobytes(), dtype=np.uint16)

# --- Compute Z = X @ W in FP16 ---
X_mat = x_fp16.reshape(M_SIZE, N_SIZE)
W_mat = w_fp16.reshape(N_SIZE, K_SIZE)

# Do the matmul in float32 for accuracy, then cast result to FP16
# (this is what the hardware does: FP16 inputs, accumulate, output FP16)
Z_mat = (X_mat.astype(np.float32) @ W_mat.astype(np.float32)).astype(np.float16)
z_bits = np.frombuffer(Z_mat.tobytes(), dtype=np.uint16)

# --- Sanity checks ---
print(f"X: {len(x_bits)} elements, range [{x_fp16.min()}, {x_fp16.max()}]")
print(f"   first 5 floats: {x_fp16[:5]}")
print(f"   first 5 hex:    {['0x%04x' % b for b in x_bits[:5]]}")
print(f"W: {len(w_bits)} elements, range [{w_fp16.min()}, {w_fp16.max()}]")
print(f"   first 5 floats: {w_fp16[:5]}")
print(f"   first 5 hex:    {['0x%04x' % b for b in w_bits[:5]]}")
print(f"Z: {len(z_bits)} elements, range [{Z_mat.min()}, {Z_mat.max()}]")
print(f"   first 5 floats: {Z_mat.flatten()[:5]}")
print(f"   first 5 hex:    {['0x%04x' % b for b in z_bits[:5]]}")

# Check for inf/nan in Z
z_flat = Z_mat.flatten()
n_inf = np.sum(np.isinf(z_flat))
n_nan = np.sum(np.isnan(z_flat))
if n_inf > 0 or n_nan > 0:
    print(f"WARNING: Z contains {n_inf} inf and {n_nan} nan values!")
    print("Consider reducing the input scale factor.")
else:
    print("All Z values are finite - no overflow!")

# --- Write header files ---
def write_header(filepath, macro_name, bits_array, row_width, comment=""):
    """
    Write a C header with a #define array macro (1D flat).
    Line breaks occur at matrix row boundaries (every row_width elements).
    """
    with open(filepath, 'w') as f:
        if comment:
            f.write(f"/* {comment} */\n")
        f.write(f"#define {macro_name} {{ \\\n")
        for i, val in enumerate(bits_array):
            f.write(f"0x{val:04x}")
            if i < len(bits_array) - 1:
                f.write(", ")
                # Break at end of each matrix row
                if (i + 1) % row_width == 0:
                    f.write(" \\\n")
        f.write(" }\n")

    # Verify the file was actually written and has content
    actual_size = os.path.getsize(filepath)
    print(f"Wrote {filepath}")
    print(f"  -> {len(bits_array)} elements, file size: {actual_size} bytes")


def write_header_2d(filepath, var_name, bits_array, rows, cols, comment=""):
    """
    Write a C header with a 2D uint16_t array declaration.
    Matches the format of the original RedMulE Golden Model 2D headers.
    Line breaks occur at matrix row boundaries (every cols elements).
    """
    with open(filepath, 'w') as f:
        if comment:
            f.write(f"/* {comment} */\n")
        f.write(f"uint16_t {var_name} [{rows}][{cols}] = {{\n")
        for i, val in enumerate(bits_array):
            f.write(f"0x{val:04x}")
            if i < len(bits_array) - 1:
                f.write(", ")
                # Break at end of each matrix row
                if (i + 1) % cols == 0:
                    f.write("\n")
        f.write("\n};\n")

    actual_size = os.path.getsize(filepath)
    print(f"Wrote {filepath}")
    print(f"  -> {var_name}[{rows}][{cols}] = {len(bits_array)} elements, file size: {actual_size} bytes")


def write_header_golden(filepath, bits_array, comment=""):
    """
    Write golden.h: packs pairs of FP16 values into uint32_t words.
    Each uint32_t = (second_fp16 << 16) | first_fp16  (little-endian).
    One value per line, matching the original golden.h format.
    """
    assert len(bits_array) % 2 == 0, "golden.h requires an even number of FP16 elements"
    n_words = len(bits_array) // 2
    with open(filepath, 'w') as f:
        if comment:
            f.write(f"/* {comment} */\n")
        f.write(f"uint32_t golden [{n_words}] = {{\n")
        for i in range(n_words):
            lo = int(bits_array[2 * i])
            hi = int(bits_array[2 * i + 1])
            word = (hi << 16) | lo
            f.write(f"0x{word:08x}")
            if i < n_words - 1:
                f.write(",\n")
        f.write("\n};\n")

    actual_size = os.path.getsize(filepath)
    print(f"Wrote {filepath}")
    print(f"  -> {n_words} uint32_t words ({len(bits_array)} FP16 values), file size: {actual_size} bytes")

# Write to test_headers/ for verification
print(f"\n--- Writing test headers to {TEST_DIR} ---")

write_header(
    os.path.join(TEST_DIR, 'x_input.h'), 'X', x_bits,
    row_width=N_SIZE,  # X is M_SIZE x N_SIZE, rows of 64
    comment=f'X input: {M_SIZE}x{N_SIZE} FP16 values (sequential 0.00, 0.01, 0.02, ...)')

write_header(
    os.path.join(TEST_DIR, 'w_input.h'), 'W', w_bits,
    row_width=K_SIZE,  # W is N_SIZE x K_SIZE, rows of 48
    comment=f'W input: {N_SIZE}x{K_SIZE} FP16 values (sequential 0.00, 0.01, 0.02, ...)')

write_header(
    os.path.join(TEST_DIR, 'z_output.h'), 'Z', z_bits,
    row_width=K_SIZE,  # Z is M_SIZE x K_SIZE, rows of 48
    comment=f'Golden output Z = X @ W: {M_SIZE}x{K_SIZE} FP16 values')

write_header(
    os.path.join(TEST_DIR, 'y_input.h'), 'Y',
    np.zeros(M_SIZE * K_SIZE, dtype=np.uint16),
    row_width=K_SIZE,  # Y is M_SIZE x K_SIZE, rows of 48
    comment=f'Y bias: {M_SIZE}x{K_SIZE} all zeros')

# --- Write 2D header files ---
print(f"\n--- Writing 2D test headers to {TEST_DIR} ---")

write_header_2d(
    os.path.join(TEST_DIR, 'x_2D.h'), 'x_inp_2D', x_bits,
    rows=M_SIZE, cols=N_SIZE,  # X is 48 x 64
    comment=f'X input 2D: {M_SIZE}x{N_SIZE} FP16 values (sequential 0.00, 0.01, 0.02, ...)')

write_header_2d(
    os.path.join(TEST_DIR, 'w_2D.h'), 'w_inp_2D', w_bits,
    rows=N_SIZE, cols=K_SIZE,  # W is 64 x 48
    comment=f'W input 2D: {N_SIZE}x{K_SIZE} FP16 values (sequential 0.00, 0.01, 0.02, ...)')

write_header_2d(
    os.path.join(TEST_DIR, 'z_2D.h'), 'z_oup_2D', z_bits,
    rows=M_SIZE, cols=K_SIZE,  # Z is 48 x 48
    comment=f'Golden output Z 2D = X @ W: {M_SIZE}x{K_SIZE} FP16 values')

write_header_2d(
    os.path.join(TEST_DIR, 'y_2D.h'), 'y_inp_2D',
    np.zeros(M_SIZE * K_SIZE, dtype=np.uint16),
    rows=M_SIZE, cols=K_SIZE,  # Y is 48 x 48
    comment=f'Y bias 2D: {M_SIZE}x{K_SIZE} all zeros')

# --- Write golden.h (packed uint32_t format) ---
print(f"\n--- Writing golden.h to {TEST_DIR} ---")

write_header_golden(
    os.path.join(TEST_DIR, 'golden.h'), z_bits,
    comment=f'Golden output Z packed as uint32_t: {M_SIZE}x{K_SIZE} = {len(z_bits)} FP16 values in {len(z_bits)//2} words')

print(f"\nDone! Verify headers in: {os.path.abspath(TEST_DIR)}/")
print("\nIf they look good, copy them to inc/")
print(f"\nExample verification: X[0,1] = {float(X_mat[0,1])}, W[1,0] = {float(W_mat[1,0])}")
print(f"Z[0,0] = sum of X[0,:]*W[:,0] = {float(Z_mat[0,0])} (hex: 0x{z_bits[0]:04x})")

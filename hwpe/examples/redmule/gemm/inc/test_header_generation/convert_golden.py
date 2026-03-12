#!/usr/bin/env python3
"""
Convert RedMulE golden-model headers from variable-declaration format
to the #define macro format expected by our (older) redmule_test.c.

Golden model outputs:          Our test harness expects:
  uint16_t x_inp [3072] = {     #define X { \
    0x39f2, 0x3281, ...          0x39f2, 0x3281, ... \
  };                             }

Also patches tensor_dim.h to add the #include "../archi_redmule.h" that
the older SDK version needs, and re-generates golden.h in the uint32_t
packed format from the converted Z data.

Usage:
    python3 convert_golden.py [--input DIR] [--output DIR] [--cols N]

    --input   Directory containing golden-model headers (default: golden_output/)
    --output  Directory to write converted headers (default: ../ i.e. inc/)
    --cols    Values per line in output (default: 10)

If --output matches the parent inc/ directory, the headers are ready to
compile immediately with `make clean all run`.
"""

import argparse
import re
import os
import sys


# Mapping: golden-model filename → (macro name, variable pattern)
# The variable pattern matches e.g. "uint16_t x_inp [3072] = {"
FILE_MAP = {
    "x_input.h":  ("X", r"uint16_t\s+\w+\s*\[\d+\]\s*=\s*\{"),
    "w_input.h":  ("W", r"uint16_t\s+\w+\s*\[\d+\]\s*=\s*\{"),
    "y_input.h":  ("Y", r"uint16_t\s+\w+\s*\[\d+\]\s*=\s*\{"),
    "z_output.h": ("Z", r"uint16_t\s+\w+\s*\[\d+\]\s*=\s*\{"),
}


def extract_hex_values(text: str) -> list[str]:
    """Pull all 0xHHHH tokens from a header file body."""
    return re.findall(r"0x[0-9a-fA-F]+", text)


def write_define_header(path: str, macro: str, values: list[str],
                        cols: int, comment: str) -> None:
    """Write a #define macro header in the format redmule_test.c expects."""
    with open(path, "w") as f:
        f.write(f"/* {comment} */\n")
        f.write(f"#define {macro} {{ \\\n")
        for i in range(0, len(values), cols):
            chunk = values[i : i + cols]
            is_last_chunk = (i + cols >= len(values))
            line = ", ".join(chunk)
            if is_last_chunk:
                f.write(f"{line} }}\n")
            else:
                f.write(f"{line}, \\\n")
    print(f"  ✓ {os.path.basename(path):20s} — {macro} macro, {len(values)} values")


def write_golden_header(path: str, z_values: list[str]) -> None:
    """Pack Z values into uint32_t pairs (little-endian: [lo, hi]) for golden.h."""
    if len(z_values) % 2 != 0:
        print(f"  ⚠ Z has odd count ({len(z_values)}), padding with 0x0000")
        z_values.append("0x0000")

    n_words = len(z_values) // 2
    with open(path, "w") as f:
        f.write(f"/* Golden output Z packed as uint32_t: {len(z_values)} FP16 values in {n_words} words */\n")
        f.write(f"uint32_t golden [{n_words}] = {{\n")
        for i in range(0, len(z_values), 2):
            lo = int(z_values[i], 16)
            hi = int(z_values[i + 1], 16)
            word = (hi << 16) | lo
            comma = "," if (i + 2) < len(z_values) else ""
            f.write(f"0x{word:08x}{comma}\n")
        f.write("};\n")
    print(f"  ✓ {'golden.h':20s} — uint32_t packed, {n_words} words")


def patch_tensor_dim(src: str, dst: str) -> None:
    """Copy tensor_dim.h, adding #include "../archi_redmule.h" if missing."""
    with open(src) as f:
        text = f.read()

    include_line = '#include "../archi_redmule.h"'
    if include_line not in text:
        # Insert after the #define __TENSOR_DIM__ guard
        text = text.replace(
            "#define __TENSOR_DIM__\n",
            f"#define __TENSOR_DIM__\n\n{include_line}\n",
        )
    with open(dst, "w") as f:
        f.write(text)
    print(f"  ✓ {'tensor_dim.h':20s} — patched with archi_redmule.h include")


def main():
    parser = argparse.ArgumentParser(
        description="Convert golden-model headers to #define macro format."
    )
    parser.add_argument("--input", default="golden_output",
                        help="Directory with golden-model headers (default: golden_output/)")
    parser.add_argument("--output", default="..",
                        help="Directory to write converted headers (default: ../)")
    parser.add_argument("--cols", type=int, default=10,
                        help="Hex values per line (default: 10)")
    args = parser.parse_args()

    if not os.path.isdir(args.input):
        print(f"Error: input directory '{args.input}' not found.")
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)

    print(f"Converting golden-model headers:")
    print(f"  Input:  {os.path.abspath(args.input)}")
    print(f"  Output: {os.path.abspath(args.output)}")
    print()

    z_values = None

    # Convert each data header
    for filename, (macro, pattern) in FILE_MAP.items():
        src = os.path.join(args.input, filename)
        if not os.path.isfile(src):
            print(f"  ⚠ {filename:20s} — not found, skipping")
            continue

        with open(src) as f:
            text = f.read()

        values = extract_hex_values(text)
        if not values:
            print(f"  ⚠ {filename:20s} — no hex values found, skipping")
            continue

        comment = f"Converted from golden-model: {macro} ({len(values)} values)"
        dst = os.path.join(args.output, filename)
        write_define_header(dst, macro, values, args.cols, comment)

        if macro == "Z":
            z_values = values

    # Generate golden.h from Z values
    if z_values is not None:
        write_golden_header(os.path.join(args.output, "golden.h"), z_values)
    else:
        print("  ⚠ No Z data found — golden.h not generated")

    # Patch tensor_dim.h
    tensor_src = os.path.join(args.input, "tensor_dim.h")
    if os.path.isfile(tensor_src):
        patch_tensor_dim(tensor_src, os.path.join(args.output, "tensor_dim.h"))
    else:
        print("  ⚠ tensor_dim.h not found in input — skipping")

    print("\nDone. Headers are ready for `make clean all run`.")


if __name__ == "__main__":
    main()

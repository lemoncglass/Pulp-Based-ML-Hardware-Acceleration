# Lemon Dot — Toy Systolic Array HWPE

A simple HWPE (Hardware Processing Engine) that computes integer matrix
multiplication **C = A × B** on the PULP cluster.  Each element `C[i][j]`
is the dot product of row `i` of A with column `j` of B — hence "lemon_dot."

This is a **learning tool** for understanding:
1. How RISC-V cores communicate with hardware accelerators via MMIO
2. The standard HWPE register protocol used by PULP accelerators (like RedMulE)
3. How to read RISC-V disassembly to see what's happening at the ISA level
4. How a multi-phase FSM models a systolic-style dataflow

## Quick Start

```bash
# Build and run
make clean all run

# Build, run, and generate disassembly
make clean all run disasm
```

## Architecture

### Register Map

The CPU programs the HWPE by writing to memory-mapped registers:

| Offset | Name       | Description                          |
|--------|------------|--------------------------------------|
| 0x00   | TRIGGER    | Write 0 to start computation         |
| 0x04   | ACQUIRE    | Read: 0 = free, 1 = busy            |
| 0x08   | FINISHED   | 1 when job complete                  |
| 0x0C   | STATUS     | 0 = idle, 1 = running               |
| 0x10   | RUNNING_JOB| Current job ID                       |
| 0x14   | SOFT_CLEAR | Write to reset state machine         |
| 0x40   | A_PTR      | Pointer to matrix A in L1            |
| 0x44   | B_PTR      | Pointer to matrix B in L1            |
| 0x48   | C_PTR      | Pointer to output matrix C in L1     |
| 0x4C   | M_SIZE     | Rows of A (and C)                    |
| 0x50   | K_SIZE     | Cols of A / Rows of B                |
| 0x54   | N_SIZE     | Cols of B (and C)                    |

### FSM Dataflow (models a systolic array)

```
IDLE ──trigger──→ LOAD_A ──→ LOAD_B ──→ COMPUTE ──→ STORE_C ──→ IRQ → IDLE
                  (M×K cy)   (K×N cy)   (M×N cy)    (M×N cy)
```

- **LOAD_A**: Stream A from L1 into PE array (weight loading)
- **LOAD_B**: Stream B from L1 into PE array (activation streaming)
- **COMPUTE**: One dot product per cycle (systolic steady-state)
- **STORE_C**: Write results back to L1

Total cycles ≈ M×K + K×N + 2×M×N (for 4×4: 64 cycles)

### ISA-Level View

There are **no special opcodes** for the HWPE.  Communication is pure MMIO:

```asm
lui   a5, 0x10201        # load HWPE base address
sw    a0, 0x040(a5)      # write A_PTR register
sw    a1, 0x044(a5)      # write B_PTR register
sw    a2, 0x048(a5)      # write C_PTR register
sw    zero, 0x000(a5)    # TRIGGER the computation
```

The one PULP ISA extension you'll see is `p.elw` (event load word),
which puts the core to sleep until the HWPE fires its completion IRQ.

## Reading the Disassembly

After `make disasm`, open `lemon_dot.dump` and:

1. **Find HWPE accesses**: `grep '10201' lemon_dot.dump`
2. **Find the event wait**: `grep 'p.elw' lemon_dot.dump`
3. **Find the driver code**: `grep 'cluster_entry' lemon_dot.dump`

## Generating New Test Vectors

```bash
python3 gen_stimuli.py                     # default 4×4
python3 gen_stimuli.py --m 8 --k 4 --n 8  # custom dimensions
python3 gen_stimuli.py --random --seed 99  # random values
```

## File Structure

```
lemon_dot/
├── archi_lemon_dot.h    # Register map (offsets & base address)
├── hal_lemon_dot.h      # HAL: inline MMIO helpers
├── lemon_dot.c          # RISC-V test application
├── Makefile             # Build + disassembly targets
├── gen_stimuli.py       # Test vector generator
├── inc/
│   ├── matrix_a.h       # Input matrix A
│   ├── matrix_b.h       # Input matrix B
│   └── golden.h         # Expected output C = A × B
├── model/
│   ├── lemon_dot.cpp    # GVSoC C++ model (systolic FSM)
│   └── lemon_dot.py     # GVSoC Python wrapper
└── README.md            # This file
```

## Comparison with RedMulE

| Feature        | Lemon Dot              | RedMulE                     |
|----------------|------------------------|-----------------------------|
| Data type      | int32                  | FP16/FP8/BF16               |
| Operation      | C = A × B              | Z = Y + X × W               |
| Dimensions     | Up to 16×16            | Up to 256×256                |
| HWPE protocol  | Same (TRIGGER/ACQUIRE) | Same (TRIGGER/ACQUIRE)       |
| ISA interaction| Pure MMIO + p.elw      | Pure MMIO + p.elw            |
| FSM            | 4-phase toy            | Complex pipeline with tiles  |

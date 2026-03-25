# Toy Adder HWPE

A minimal Hardware Processing Engine (HWPE) example for the PULP-open platform
on GVSoC.  It reads two 32-bit integers from L1 memory, computes their sum, and
writes the result back — just enough to exercise the full HWPE datapath without
any algorithmic complexity.

```
A (42) + B (17) = 59   ✓
```

## File layout

```
toy_adder/
├── archi_toy_adder.h       # Register map (base address, control/job offsets)
├── hal_toy_adder.h         # HAL — thin read/write helpers over the register map
├── toy_adder.c             # Test program (allocates L1 data, programs HWPE, checks result)
├── Makefile                # pulp-sdk build; passes model path via --target-property
├── testset.cfg             # GVSoC test-set entry
├── inc/
│   ├── a_input.h           # OPERAND_A  = 42
│   ├── b_input.h           # OPERAND_B  = 17
│   └── golden.h            # GOLDEN_SUM = 59
└── model/
    ├── toy_adder.py        # GVSoC Python wrapper (ports + add_sources)
    └── toy_adder.cpp       # GVSoC C++ simulation model (FSM, MMIO, L1 access)
```

## Prerequisites

1. The repo-level environment has been set up (`setup.sh` / `setup_env.sh`).
2. GVSoC has been built at least once (`cd gvsoc && make clean all`).
3. The custom-HWPE cluster wiring has been installed — see
   [`hwpe/files to add to gvsoc/`](../files%20to%20add%20to%20gvsoc/) and run
   one of its install scripts.

## Building and running

```bash
cd /path/to/Pulp-Based-ML-Hardware-Acceleration   # repo root
source setup_env.sh
cd hwpe/toy_adder
make clean all run
```

Expected output:

```
[toy_adder] Operand A (L1) = 42
[toy_adder] Operand B (L1) = 17
[toy_adder] Result         = 59
[toy_adder] Golden         = 59

[toy_adder] PASS
```

## How it works

### Software side

1. `toy_adder.c` allocates three `uint32_t` values in L1 (`pi_l1_malloc`).
2. It writes `OPERAND_A` and `OPERAND_B` to L1, then programs the HWPE with
   their addresses via the HAL (`hwpe_set_a`, `hwpe_set_b`, `hwpe_set_res_ptr`).
3. `hwpe_trigger_job()` writes to the TRIGGER register, then the core sleeps on
   `eu_evt_maskWaitAndClr(1 << TOY_ADDER_EVT0)` until the HWPE fires its IRQ.
4. On wake-up it compares the L1 result against `GOLDEN_SUM`.

### Hardware (model) side

1. The **Python wrapper** (`toy_adder.py`) tells GVSoC where the C++ source
   lives (`add_sources`) and declares three ports: MMIO slave (`input`), L1
   master (`out`), and IRQ wire (`irq`).
2. The **C++ model** (`toy_adder.cpp`) implements:
   - An MMIO handler that accepts register reads/writes for job configuration
     and control (TRIGGER, ACQUIRE, STATUS, SOFT_CLEAR).
   - A single-cycle FSM that reads A and B from L1 via the `out` port, computes
     A + B, writes the result back, and asserts the IRQ line.

### Cluster wiring

The Makefile passes `--target-property=chip/cluster/custom_hwpe=<path>` to
GVSoC.  The modified `cluster.py` in `hwpe/files to add to gvsoc/` dynamically
loads the Python wrapper at that path and wires the HWPE into the cluster:

- **MMIO** — mapped at `0x10201000` through the peripheral interconnect.
- **L1** — routed through a `Router` (strips the L1 base address) into the
  shared interleaver port `nb_pe + 4`.
- **IRQ** — connected to the `acc_0` event line (index 12) on the event unit.

## Adapting for a new HWPE

This example is designed as a template.  To create a new accelerator:

1. Copy `toy_adder/` to a new directory under `hwpe/`.
2. Rewrite the register map (`archi_*.h`), HAL (`hal_*.h`), and test program.
3. Rewrite the C++ model with your accelerator's logic.
4. Update `toy_adder.py` → your model wrapper (keep the same port signatures).
5. Update the Makefile's `--target-property` to point to your new `.py`.
6. Re-run the install script to recompile the `.so` (or compile manually).

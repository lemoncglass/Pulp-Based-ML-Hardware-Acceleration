# Files to add to GVSoC

This directory contains the three Python/JSON files that wire the **custom HWPE
slot** into the PULP-open cluster configuration:

| File               | What it does |
|--------------------|--------------|
| `cluster.py`       | Declares the `custom_hwpe` string property, dynamically loads the model `.py` at the path you give it, and wires the MMIO, IRQ, and L1 ports. |
| `cluster.json`     | Adds the peripheral address mapping for `custom_hwpe` (base `0x10201000`). |
| `l1_subsystem.py`  | Routes the HWPE's L1 master port through a `Router` that strips the L1 base address before reaching the interleaver. |

They replace the **originals** that ship with gvsoc-pulp.

---

## Quick-start (two paths, pick one)

Both scripts copy the generator files **and** compile the toy_adder C++ model
(`.so`) in one shot.  Run them from anywhere — they find the repo root
automatically.

### Option A — Fast path (no rebuild)

Copies straight into the GVSoC **install** tree.  Instant, but gets overwritten
if you later run `make clean all` in `gvsoc/`.

```bash
./hwpe/files\ to\ add\ to\ gvsoc/install_quick.sh
```

### Option B — Permanent (survives `make clean all` in gvsoc/)

Copies into the GVSoC **source** tree, then runs `cmake --install` to
propagate.  Persists across rebuilds.

```bash
./hwpe/files\ to\ add\ to\ gvsoc/install_permanent.sh
```

> **Why two directories?**  GVSoC's CMake build copies the Python generators
> from the source tree (`gvsoc/pulp/pulp/chips/pulp_open/`) into the install
> tree (`gvsoc/install/generators/pulp/chips/pulp_open/`) during
> `cmake --install`.  At runtime, `gvrun` only reads from the **install** tree.
> Option A edits the install tree directly (fast, but gets overwritten by a
> rebuild).  Option B edits the source tree so it persists across rebuilds.

---

## What the scripts do

Each script performs two steps:

1. **Copies the Python / JSON generator files** (`cluster.py`, `cluster.json`,
   `l1_subsystem.py`) — these wire the custom HWPE slot into the cluster.

2. **Compiles the toy_adder C++ model** (`toy_adder.cpp` → `.so`) — GVSoC's
   JIT `add_sources()` system registers the source at config-generation time,
   but compilation happens during the CMake build, which doesn't know about
   models specified at run time via `--target-property`.  The script compiles
   it manually and places the `.so` where GVSoC expects it.

> **Note:** The `.so` name encodes the *absolute* path to the source file, so
> it will differ between machines.  The scripts compute it automatically.

---

## Verifying everything works

```bash
cd /path/to/Pulp-Based-ML-Hardware-Acceleration   # <-- adjust this to your repo root

source setup_env.sh
cd hwpe/toy_adder
make clean all run
```

You should see:

```
[toy_adder] Operand A (L1) = 42
[toy_adder] Operand B (L1) = 17
[toy_adder] Result         = 59
[toy_adder] Golden         = 59

[toy_adder] PASS
```
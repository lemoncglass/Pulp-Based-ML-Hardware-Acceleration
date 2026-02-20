# Pulp-Based-ML-Hardware-Acceleration

Hardware accelerators for ML algorithms on the [PULP](https://pulp-platform.org/) (Parallel Ultra-Low-Power Processing) platform — an open-source RISC-V parallel processing system for efficient neural network inference on resource-constrained devices.

## Quick Start

```bash
git clone https://github.com/lemoncglass/Pulp-Based-ML-Hardware-Acceleration.git
cd Pulp-Based-ML-Hardware-Acceleration
bash setup.sh          # installs everything (~30-60 min, mostly toolchain compilation)
source setup_env.sh    # set PATH & SDK config (run this every new terminal)
cd pulp-sdk/tests/hello && make clean all run   # should print "Hello from FC"
```

That's it. `setup.sh` handles all cloning, patching, and building automatically.

## Prerequisites

- **OS:** Ubuntu 20.04+ (tested on 24.04 x86_64)
- **Disk space:** ~10 GB
- **Time:** 30–60+ minutes (toolchain cross-compilation)
- **sudo access** for `apt-get install`

## What `setup.sh` Does

1. Installs system packages (`build-essential`, `cmake`, `texinfo`, etc.)
2. Clones the [PULP RISC-V GNU toolchain](https://github.com/pulp-platform/pulp-riscv-gnu-toolchain) and applies two missing-file patches (see below)
3. Builds the cross-compiler (`riscv32-unknown-elf-gcc`) → installs to `~/riscv/`
4. Clones and builds the [PULP SDK](https://github.com/pulp-platform/pulp-sdk)
5. Clones and builds the [GVSoC](https://github.com/gvsoc/gvsoc) virtual platform simulator
6. Clones [PULP-NN](https://github.com/pulp-platform/pulp-nn) and [PULP-Train](https://github.com/pulp-platform/pulp-train)

> **Note:** All third-party repos are `.gitignored` — they are cloned fresh by `setup.sh`.
> Only our own code, scripts, patches, and the NN kernels are committed.

## Toolchain Patches

As of February 2026, the PULP fork of the RISC-V toolchain is missing two files that cause silent build failures. These are shipped in `patches/` and applied automatically by `setup.sh`:

| Patch file | Destination | Without it |
|---|---|---|
| `patches/sysroff.info` | `pulp-riscv-gnu-toolchain/riscv-binutils-gdb/binutils/sysroff.info` | `No rule to make target 'sysroff.info'` — binutils fails |
| `patches/newlib-sys-config.h` | `pulp-riscv-gnu-toolchain/riscv-newlib/newlib/libc/include/sys/config.h` | `fatal error: sys/config.h` — newlib fails, then `stdint.h` missing later |

If you're building manually (without `setup.sh`), copy them yourself before running `make newlib`.

## Environment Setup

Every time you open a new terminal:

```bash
source setup_env.sh
```

This sets `PULP_RISCV_GCC_TOOLCHAIN`, adds the toolchain and GVSoC to your `PATH`, and sources the SDK config.

## Verify Installation

```bash
cd pulp-sdk/tests/hello
make clean all run
```

Expected output: **"Hello from FC"** from the GVSoC simulator.

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| `riscv32-unknown-elf-gcc: No such file or directory` | Toolchain build failed or incomplete | Check `pulp-riscv-gnu-toolchain/stamps/` — re-run `make newlib` |
| `No rule to make target 'sysroff.info'` | Missing patch file | `cp patches/sysroff.info pulp-riscv-gnu-toolchain/riscv-binutils-gdb/binutils/` |
| `fatal error: sys/config.h` | Missing patch file | `cp patches/newlib-sys-config.h pulp-riscv-gnu-toolchain/riscv-newlib/newlib/libc/include/sys/config.h` |
| `fatal error: stdint.h` | Newlib build failed silently | Fix `sys/config.h` first, then `cd pulp-riscv-gnu-toolchain && rm -rf build-newlib stamps/build-newlib && make newlib` |
| `~/riscv/bin/` is empty | Binutils failed (likely `sysroff.info`) | Apply patches and rebuild from scratch |
| `gvsoc: No such file or directory` | GVSoC not built | Re-run `setup.sh` or build manually (see setup.sh step 4) |
| `externally-managed-environment` (pip) | Ubuntu 24.04+ PEP 668 | `setup.sh` handles this; manually add `--break-system-packages` to pip |
| `cmake: command not found` | cmake missing | `sudo apt-get install -y cmake` |

### Resuming a Partial Build

The toolchain tracks progress with stamp files. Re-running `make newlib` skips completed stages. To force a rebuild of a specific stage:

```bash
cd pulp-riscv-gnu-toolchain
rm -rf build-<stage> stamps/build-<stage>
make newlib
```

Stages (in order): `binutils-newlib` → `gcc-newlib-stage1` → `newlib` → `gcc-newlib-stage2`

## Repo Structure

```
├── README.md
├── setup.sh              # One-time full setup (clones, patches, builds everything)
├── setup_env.sh          # Source every terminal session
├── patches/              # Fixes for upstream PULP toolchain bugs
│   ├── sysroff.info
│   └── newlib-sys-config.h
├── pulp-nn/              # Optimized NN kernels (committed — our focus)
│   ├── 32bit/
│   └── 64bit/
│
│  ── .gitignored (cloned by setup.sh) ──────────────────────────
├── pulp-riscv-gnu-toolchain/   # RISC-V cross-compiler → ~/riscv/
├── pulp-sdk/                   # SDK, RTOS, tests
├── gvsoc/                      # GVSoC virtual platform simulator
└── pulp-train/                 # Training tools
```

## Next Steps

- **Learn the basics:** Start with examples in `pulp-sdk/tests/`
- **ML on PULP:** PULP-NN provides quantized kernels for convolutions, pooling, and linear layers
- **Create a project:** Copy `pulp-sdk/tests/hello/` as a template, then add PULP-NN kernels

## Resources

- [PULP Platform](https://pulp-platform.org/)
- [PULP-NN](https://github.com/pulp-platform/pulp-nn)
- [PULP SDK](https://github.com/pulp-platform/pulp-sdk)
- [GVSoC Simulator](https://github.com/gvsoc/gvsoc)
- [RISC-V GNU Toolchain (PULP fork)](https://github.com/pulp-platform/pulp-riscv-gnu-toolchain)
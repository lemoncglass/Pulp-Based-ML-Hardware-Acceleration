# Lemon Dot HWPE

The Lemon Dot (Matrix **Dot** Product Engine) is a toy Hardware Processing Engine (HWPE) example for the PULP-open platform on GVSoC. Heavily based on [RedMulE](../examples/redmule/).  
// TODO: Finish explanation

```
*example calculation*
```

## File layout

```
lemon_dot/
├── archi_lemon_dot.h       # Register map (base address, control/job offsets)
├── hal_lemon_dot.h         # HAL — thin read/write helpers over the register map
├── lemon_dot.c             # Test program (allocates L1 data, programs HWPE, checks result)
├── Makefile                # pulp-sdk build; passes model path via --target-property
├── testset.cfg             # GVSoC test-set entry
├── inc/
│   └── *inputs*
└── model/
    ├── lemon_dot.py        # GVSoC Python wrapper (ports + add_sources)
    └── lemon_dot.cpp       # GVSoC C++ simulation model (FSM, MMIO, L1 access)
```

## Prerequisites

1. The repo-level environment has been set up (`setup.sh` / `setup_env.sh`).
2. GVSoC has been built at least once (`cd gvsoc && make clean all`).
3. The custom-HWPE cluster wiring has been installed — see
   [`hwpe/files-to-add-to-gvsoc/`](../files-to-add-to-gvsoc/) and run one of its install scripts.

## Building and running

navigate to the repo root with something like:
```bash
# not copy-paste-able
cd /path/to/Pulp-Based-ML-Hardware-Acceleration   # repo root
```
*(do not just copy-paste **/path/to/** you just need to be in the repo root **Pulp-Based-ML-Hardware-Acceleration**)*

```bash
source setup_env.sh
cd hwpe/lemon_dot
make clean all run
```

Expected output:

```
*output*
```

## How it works

*see [lemon_adder/README.md — How it works](../lemon_adder/README.md#how-it-works) for an overview*
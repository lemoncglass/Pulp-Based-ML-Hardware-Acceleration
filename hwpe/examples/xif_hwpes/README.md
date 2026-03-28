# XIF HWPE Reference — LightRedMulE + Magia Infrastructure

This directory contains an **annotated, split-up copy** of the LightRedMulE
GVSoC model and the Magia chip infrastructure needed to understand how
RISC-V custom instructions interact with hardware accelerators via the
**eXtension Interface (XIF)**.

> **Note:** These are reference copies for reading/learning. The original
> (buildable) sources live under `gvsoc/pulp/pulp/`.

---

## Directory Layout

```
xif_hwpes/
├── README.md                   ← You are here
│
├── model/                      ← LightRedMulE C++ model (split from 1 file → 10)
│   ├── light_redmule_types.h       Types, enums, FP typedefs
│   ├── matmul_kernels.h             Forward declarations for matmul functions
│   ├── light_redmule.h             Class definition (all member vars & methods)
│   ├── light_redmule_init.cpp      Constructor, port setup, buffer allocation
│   ├── light_redmule_tiling.cpp    Tiling math, address generation, iteration
│   ├── light_redmule_compute.cpp   Buffer fill/drain + matmul kernel dispatch
│   ├── light_redmule_offload.cpp   XIF handler (mcnfig / marith instructions)
│   ├── light_redmule_mmio.cpp      MMIO register read/write handler
│   ├── light_redmule_fsm.cpp       Cycle-driven FSM (PRELOAD→ROUTINE→STORING)
│   └── matmul_kernels.cpp          All matmul kernels + FP conversion utilities
│
└── magia/                      ← Magia chip infrastructure (annotated copies)
    ├── tile.py                      Tile wiring: core + xifdec + redmule + iDMA
    ├── xif_decoder.cpp              Opcode router (routes custom insns to copros)
    ├── xif_decoder.py               Python wrapper for XifDecoder
    ├── isa.py                       ISA definitions (mcnfig, marith, iDMA, fsync)
    ├── light_redmule_iss.hpp        ISS-side instruction handlers (core side)
    ├── light_redmule.py             Python wrapper for LightRedmule
    ├── hwpe_interleaver.cpp         TCDM bank interleaver for RedMulE
    └── hwpe_interleaver.py          Python wrapper for HWPEInterleaver
```

---

## How XIF Works — The Full Data Flow

When the core encounters a custom instruction (e.g., `mcnfig` or `marith`),
here's exactly what happens at each stage:

### 1. ISS Decode (Core Side)

The CV32E40X ISS matches the opcode against the ISA definitions in
[magia/isa.py](magia/isa.py). For example, `mcnfig` matches:

```
opcode[6:0] = 0b0001011  (custom-0)
```

The ISS calls `mcnfig_exec()` from [magia/light_redmule_iss.hpp](magia/light_redmule_iss.hpp):

```cpp
IssOffloadInsn offload_insn = {
    .opcode = insn->opcode,
    .arg_a  = REG_GET(0),   // rs1 = {K[31:16], M[15:0]}
    .arg_b  = REG_GET(1),   // rs2 = N
};
iss->exec.offload_insn(&offload_insn);  // Fire over XIF wire!
```

### 2. XIF Decoder (Router)

The offload wire reaches the XifDecoder
([magia/xif_decoder.cpp](magia/xif_decoder.cpp)), which checks
`opcode[6:0]` and routes:

```
0b0001011 → RedMulE  (slave port S2)
0b0101011 → RedMulE  (slave port S2)
0b1111011 → iDMA     (slave port S1)
0b1011011 → iDMA or FractalSync (depends on funct3)
```

### 3. LightRedMulE (Accelerator)

The instruction arrives at `LightRedmule::offload_sync()`
([model/light_redmule_offload.cpp](model/light_redmule_offload.cpp)):

- **mcnfig:** extracts M, N, K from `arg_a`/`arg_b`, sets `granted=true`
- **marith:** extracts X/W/Y pointers + format, initializes tiling, starts FSM

### 4. FSM Execution

The FSM ([model/light_redmule_fsm.cpp](model/light_redmule_fsm.cpp)) runs
one TCDM request per cycle:

```
PRELOAD   → Load initial X tile + Y tile into local buffers
ROUTINE   → For each (i,j,k) tile:
            • Load W tile (overlapped with compute)
            • Compute Z += X·W via matmul kernel
            • Prefetch next X, Y tiles
            • Store previous Z tile
STORING   → Write final Z tile to TCDM
FINISHED  → Fire IRQ (XIF mode) or respond to stalled core (MMIO mode)
```

### 5. Compute Kernels

The actual math is in [model/matmul_kernels.cpp](model/matmul_kernels.cpp):
a standard triple-nested loop for each supported format (FP16, FP8, INT16,
INT8, UINT16, UINT8).

---

## XIF vs. MMIO — Two Ways to Talk to RedMulE

| Feature | MMIO Mode | XIF Mode |
|---------|-----------|----------|
| Core | CV32E40P (RI5CY) | CV32E40X |
| Config | Store to registers at base+0x00..0x18 | `mcnfig rs1, rs2` |
| Trigger | Load from base+0x20 (sync) or +0x24 (async) | `marith rs1, rs2, rs3, imm` |
| Wait | Load from base+0x28 | Poll status or wait for IRQ |
| Code in model | [light_redmule_mmio.cpp](model/light_redmule_mmio.cpp) | [light_redmule_offload.cpp](model/light_redmule_offload.cpp) |
| Chip | PULP-Open | Magia |

---

## Tile Wiring Diagram

```
┌─────────────────────────────────────────────────────────┐
│                     Magia Tile                          │
│                                                         │
│  ┌──────────┐    offload    ┌────────────┐              │
│  │ CV32E40X ├──────────────►│ XifDecoder │              │
│  │  (core)  │◄──────────────┤            │              │
│  └────┬─────┘    grant      └───┬────┬───┘              │
│       │                   S2 │  │ S1                    │
│       │ data        ┌───────┘  └───────┐                │
│       │             ▼                  ▼                 │
│       │      ┌──────────────┐  ┌─────────────┐          │
│       │      │ LightRedMulE │  │ iDMA Ctrl   │          │
│       │      │  (this model)│  │ + iDMA0/1   │          │
│       │      └──────┬───────┘  └──────┬──────┘          │
│       │             │ tcdm            │ tcdm            │
│       │             ▼                 ▼                  │
│       │      ┌──────────────────────────────┐           │
│       └─────►│          TCDM Banks          │           │
│     OBI xbar │  (banked scratchpad memory)  │           │
│              └──────────────────────────────┘           │
└─────────────────────────────────────────────────────────┘
```

---

## Instruction Encoding Quick Reference

### mcnfig (configure matrix dimensions)

```
31       25 24   20 19   15 14  12 11    7 6      0
┌──────────┬───────┬───────┬──────┬───────┬────────┐
│ 0000000  │  rs2  │  rs1  │ 000  │  rd   │0001011 │
└──────────┴───────┴───────┴──────┴───────┴────────┘
                     │         │
                     │         └── N_SIZE
                     └── {K_SIZE[31:16], M_SIZE[15:0]}
```

### marith (configure addresses + trigger)

```
31   27 26 25 24  20 19   15 14  12 11     7 6      0
┌───────┬────┬──────┬───────┬──────┬────────┬────────┐
│  rs3  │ 00 │ rs2  │  rs1  │ fmt  │imm[7:0]│0101011 │
└───────┴────┴──────┴───────┴──────┴────────┴────────┘
   │              │       │                │
   │              │       │                └── {op_sel[5:3], format[2:0]}
   │              │       └── X_addr (input activation)
   │              └── W_addr (weights)
   └── Y_addr (bias / output)
```

---

## Reading Order (Suggested)

1. **[magia/isa.py](magia/isa.py)** — See how opcodes are defined
2. **[magia/light_redmule_iss.hpp](magia/light_redmule_iss.hpp)** — How the core packages and fires instructions
3. **[magia/xif_decoder.cpp](magia/xif_decoder.cpp)** — How opcodes get routed
4. **[model/light_redmule_offload.cpp](model/light_redmule_offload.cpp)** — How RedMulE receives XIF instructions
5. **[model/light_redmule.h](model/light_redmule.h)** — Full class definition with all member variables
6. **[model/light_redmule_init.cpp](model/light_redmule_init.cpp)** — Construction and port setup
7. **[model/light_redmule_tiling.cpp](model/light_redmule_tiling.cpp)** — Tiling strategy
8. **[model/light_redmule_fsm.cpp](model/light_redmule_fsm.cpp)** — The main execution loop
9. **[model/light_redmule_compute.cpp](model/light_redmule_compute.cpp)** — Buffer management
10. **[model/matmul_kernels.cpp](model/matmul_kernels.cpp)** — The actual math
11. **[magia/tile.py](magia/tile.py)** — How everything is wired together

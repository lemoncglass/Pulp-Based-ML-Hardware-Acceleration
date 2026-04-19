# XLemon Adder HWPE (XIF-only)

A toy Hardware Processing Engine (HWPE) that communicates **exclusively
through the RISC-V eXtension Interface (XIF)** — no memory-mapped registers.
It reads two 32-bit integers from L1 memory, computes their sum, and writes the
result back, using a single custom RISC-V instruction (`xladd`).

```
xladd a0, a1, a2   →   *a0 + *a1 → *a2   →   17 + 42 = 59  ✓
```

This is a fully working, self-contained example — `make sim` runs end-to-end on
a custom GVSoC target with a bare-metal test (no PULP SDK runtime required).

## Quick start

```bash
# 1. One-time setup (if not already done)
bash setup.sh              # from repo root — builds everything including xlemon .so files

# 2. Every new terminal
source setup_env.sh        # from repo root — sets PATH, GVSoC env, SDK config

# 3. Build and run
cd hwpe/xlemon_adder
make sim
```

Expected output:
```
Test 1 (software add): 17 + 42 = 59 ... PASS
Test 2 (xladd custom instruction): 17 + 42 = 59 ... PASS
ALL TESTS PASSED
```

## XIF vs MMIO — Side by Side

| Aspect | MMIO (lemon_adder) | XIF (xlemon_adder) |
|--------|-------------------|-------------------|
| Core | CV32E40P (RI5CY) | CV32E40X (with XIF) |
| Config | 3× store to registers at base+0x40..0x48 | Pack pointers into rs1/rs2/rs3 |
| Trigger | Store to base+0x00 (TRIGGER) | Execute `xladd` custom instruction |
| Total ops | 4 stores + 1 IRQ wait | 1 instruction + 1 IRQ wait |
| Model port | `IoSlave` (MMIO input) | `WireSlave<IssOffloadInsn*>` |
| Address map | Needs base address in periph interconnect | None — opcodes are routed |
| Wiring | periph_ico → HWPE input | XifDecoder → HWPE offload |

## File layout

```
xlemon_adder/
├── README.md
├── Makefile                    # 'make sim' builds bare-metal ELF + runs gvsoc
│
├── test_bare.c                 # Bare-metal test (sw add + xladd, no SDK)
├── crt0_bare.S                 # Minimal startup: set sp, call main
├── bare.ld                     # Linker script: L2 text + L1 data
│
├── archi_xlemon_adder.h        # Instruction encoding (opcode, register fields)
├── hal_xlemon_adder.h          # HAL — inline asm helper to fire xladd
├── xlemon_adder_test.c         # SDK-based test (not used by 'make sim')
├── inc/
│   ├── a_input.h               # OPERAND_A  = 42
│   ├── b_input.h               # OPERAND_B  = 17
│   └── golden.h                # GOLDEN_SUM = 59
│
├── model/                      # Accelerator GVSoC model
│   ├── xlemon_adder.py         # Python wrapper (XIF ports: offload + grant)
│   ├── xlemon_adder.cpp        # C++ model (offload_sync handler + FSM)
│   └── exit_module.cpp         # ExitModule: MMIO write → simulation quit
│
├── chip/                       # Custom GVSoC target ("xlemon_target")
│   ├── xlemon_target.py        # Top-level target (chip description)
│   ├── tile.py                 # Tile wiring: core ↔ xifdec ↔ xlemon ↔ L1/L2
│   ├── core.py                 # Core config (CV32E40X, ISA subsets)
│   ├── isa.py                  # Rv32xlemon ISA subset with xladd instruction
│   ├── class.hpp               # ISS class header for xlemon core
│   ├── xlemon_adder_xif.hpp    # xladd_exec() — ISS handler, packs rs1/rs2/rs3
│   ├── xif_decoder.py          # XifDecoder Python wrapper
│   ├── xif_decoder.cpp         # XifDecoder C++ — routes opcode → xlemon
│   └── exit_module.py          # ExitModule Python wrapper
│
├── build_iss.sh                # Builds the ISS core .so (22 sources + cflags)
└── build_xlemon_target.sh      # Builds XifDecoder + XLemonAdder .so files
```

## Memory map

|    Region    |     Base     |        Size       |                    Purpose                      |
|--------------|--------------|-------------------|-------------------------------------------------|
| L2 (program) | `0x1C000000` | 512 KB            | Code + read-only data + stack                   |
| L1 TCDM      | `0x10000000` | 128 KB (16 banks) | Accelerator operands (`.l1_data` section)       |
| low_mem      | `0x00000000` | 64 KB             | Boot vectors                                    |
| stdout       | `0x1A10F000` | 4 B               | `putchar()` — write a byte here to print        |
| exit         | `0x1A10E000` | 4 B               | Write any value → `ExitModule` quits simulation |

**Stack**: `bare.ld` places the stack at L2 end (`0x1C000000 + 512K = 0x1C080000`).
The stack **must** stay within L2 bounds — an earlier bug had ORIGIN at `0x1C008000`
which put the stack at `0x1C088000`, outside the memory region, causing silent failures.

## How it works

### The xladd custom instruction

Encoding: `0x60B5002B` (with rs1=a0, rs2=a1, rs3=a2)

```
 [31:27] [26:25] [24:20] [19:15] [14:12] [11:7] [6:0]
┌───────┬──────┬───────┬───────┬───────┬───────┬─────────┐
│  rs3  │  00  │  rs2  |  rs1  │  000  │ 00000 │ 0101011 │
└───────┴──────┴───────┴───────┴───────┴───────┴─────────┘
   │              │       │                         │
   │              │       └── A pointer             └── opcode (custom-1)
   │              └── B pointer
   └── result pointer
```

In `test_bare.c` this is emitted as:
```c
asm volatile (".word 0x60B5002B" ::: "memory");
```
where `a0`/`a1`/`a2` are pre-loaded with pointers to L1 operands.

### Data flow through the XIF pipeline

```
 ┌──────────────────────────────────────────────────────────┐
 │  1. Software loads pointers into a0, a1, a2              │
 │  2. Emits .word 0x60B5002B (xladd encoding)              │
 │                     ↓                                    │
 │  3. CV32E40X ISS doesn't recognize opcode 0b0101011      │
 │     → packages rs1,rs2,rs3 into IssOffloadInsn           │
 │     → fires offload wire                                 │
 │                     ↓                                    │
 │  4. XifDecoder checks opcode[6:0]                        │
 │     → routes to XLemonAdder slave port                   │
 │                     ↓                                    │
 │  5. XLemonAdder::offload_sync() receives it              │
 │     → sets granted=true (core can continue)              │
 │     → extracts A_ptr, B_ptr, RES_ptr                     │
 │     → schedules FSM                                      │
 │                     ↓                                    │
 │  6. FSM reads A from TCDM, reads B from TCDM             │
 │     → computes A + B                                     │
 │     → writes result to TCDM                              │
 │     → fires IRQ                                          │
 │                     ↓                                    │
 │  7. Core wakes up, reads result from L1                  │
 └──────────────────────────────────────────────────────────┘
```

### Bare-metal test (test_bare.c)

1. Declares three `int32_t` in `.l1_data` section (mapped to L1 TCDM at `0x10000000`).
2. **Test 1**: software add (`a + b`) to verify memory/printf work.
3. **Test 2**: loads `&a`, `&b`, `&res` into `a0`, `a1`, `a2`, emits `.word 0x60B5002B`.
4. Busy-waits for result (polls `res` — no IRQ in bare-metal mode).
5. Calls `gvsoc_exit(0)` which writes to `0x1A10E000` to cleanly end simulation.

### Hardware (model) side

1. **Python wrapper** (`xlemon_adder.py`) declares XIF ports (`i_OFFLOAD`,
   `o_OFFLOAD_GRANT`) instead of the MMIO slave port (`i_INPUT`).
2. **C++ model** (`xlemon_adder.cpp`) implements `offload_sync()` — the XIF
   callback that replaces the MMIO `hwpe_slave()` handler.  Same FSM, same
   memory access, same IRQ — just a different entry point.

## Building the .so files

GVSoC loads C++ models as shared libraries (`.so` files) from
`gvsoc/install/models/`. Four custom `.so` files are needed for the xlemon
target (ISS core, XifDecoder, XLemonAdder, ExitModule). Stock components
(memory, router) are already built by `setup.sh`.

**`make sim` handles everything automatically** — it calls `build_xlemon_target.sh`
which discovers the required components, skips any that are already built, and
compiles the rest. On a fresh clone after `setup.sh`, the first `make sim` will
take ~30 seconds to compile all 4 custom components; subsequent runs skip the
build in ~1 second.

To rebuild manually (e.g., after editing a `.cpp` model):
```bash
# Delete the old .so and rebuild
rm gvsoc/install/models/gen_*xlemon_adder*
./build_xlemon_target.sh
```

### How build_xlemon_target.sh works

1. Runs `gvsoc ... components` to discover the exact `.so` names GVSoC expects
   (names include a hash of sources + cflags, and encode absolute paths)
2. Parses the generated `components.cfg` for source lists and cflags
3. Skips components whose `.so` already exists
4. Compiles missing ones — `.c` files (like `flexfloat.c`) are compiled with
   `gcc` separately because they use `try` as a variable name (C++ keyword),
   then linked together

### .so naming convention

GVSoC auto-generates .so names using: `gen_<sanitized_first_source>_<hash>`.
The hash is computed as:
```python
int(hashlib.md5(''.join(sources + cflags)).hexdigest()[:7], 16)
```
For manually compiled .so files, just use a simple name and set `vp_component`
in the Python wrapper to match (e.g., `exit_module` → `exit_module.so`).

## Tile wiring (chip/tile.py)

The tile connects all components:

```
core.o_OFFLOAD ──→ xifdec.i_OFFLOAD_M
xifdec.o_OFFLOAD_S0 ──→ xlemon.i_OFFLOAD
xlemon.o_OFFLOAD_GRANT ──→ xifdec.i_OFFLOAD_GRANT_S0
xlemon.o_OUT ──→ l1_ico (TCDM)
core ──→ l1_ico, l2, stdout, exit_module (via memory map)
loader ──→ l1_ico, l2, low_mem (binary loading)
```

## Important gotchas & lessons learned

### 1. GDBServer blocks simulation reset
If `tile.py` instantiates a `GDBServer` component, it will block waiting for a
GDB connection on reset. **Comment it out** unless you actually need GDB.

### 2. Semihosting exit does NOT work
The standard RISC-V semihosting exit sequence (`slli x0,x0,0x1f` + `ebreak`)
does not work reliably in this setup. The ISS checks `insn_cache.get_insn(pc-4)`
to see if the previous instruction was the semihosting marker, but the cache
lookup returns wrong/uninitialized data. **Use the ExitModule instead** — a simple
MMIO write to `0x1A10E000` that calls `engine->quit()`.

### 3. Stack must be within L2 bounds
`bare.ld` must have `ORIGIN = 0x1C000000` (not `0x1C008000`). The stack sits at
the end of L2. If ORIGIN is wrong, the stack lands outside the memory region,
causing silent corruption or hangs.

### 4. L1 data must be in `.l1_data` section
The accelerator reads operands from L1 TCDM. Variables must be placed in the
`.l1_data` section (mapped to `0x10000000` by `bare.ld`), not on the stack.

### 5. PULP SDK runtime is not needed
The SDK's `pmsis_rules.mk` and `pulpos` runtime target `pulp-open` which uses
CV32E40P — no XIF support. The bare-metal approach (`test_bare.c` + `crt0_bare.S`
+ `bare.ld`) bypasses the SDK entirely. The Makefile uses `-include` (with dash)
so it won't fail if `pmsis_rules.mk` is absent.

### 6. Instruction encoding matters
The `.word` encoding `0x60B5002B` assumes `rs1=a0(x10)`, `rs2=a1(x11)`,
`rs3=a2(x12)`. If you use different registers, you must recompute the encoding.
The opcode is always `0b0101011` (custom-1).

## Reference

The `xif_hwpes/` reference directory under `hwpe/examples/` shows these
patterns implemented for LightRedMulE on the Magia chip.

|      Piece     |      Reference file     |                xlemon equivalent                |
|----------------|-------------------------|-------------------------------------------------|
| ISA definition | `isa.py`                | `chip/isa.py` — `Rv32xlemon(IsaSubset)`         |
| ISS handler    | `light_redmule_iss.hpp` | `chip/xlemon_adder_xif.hpp` — `xladd_exec()`    |
| XifDecoder     | `xif_decoder.cpp`       | `chip/xif_decoder.cpp`                          |
| Tile wiring    | `tile.py`               | `chip/tile.py`                                  |
| Exit mechanism | (semihosting)           | `model/exit_module.cpp` + `chip/exit_module.py` |

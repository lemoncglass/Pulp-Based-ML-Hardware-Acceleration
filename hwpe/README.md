# Creating a Custom HWPE

This directory is the home for Hardware Processing Engine (HWPE) accelerator
designs on the PULP-open platform (GVSoC).  The guide below walks through how
to create a new one from scratch.  A working reference implementation lives in
[`toy_adder/`](toy_adder/) — it adds two integers and is intentionally minimal
so the plumbing is easy to follow.

---

## Step 0 — One-time GVSoC setup

Before any custom HWPE will work, the GVSoC cluster config needs a
**`custom_hwpe` slot** patched in.  This only has to be done once per machine.

Run one of the install scripts in
[`files to add to gvsoc/`](files%20to%20add%20to%20gvsoc/):

```bash
# Quick — edits the install tree directly (doesn't survive a GVSoC rebuild):
./hwpe/files\ to\ add\ to\ gvsoc/install_quick.sh

# Permanent — edits the source tree + re-installs (survives rebuilds):
./hwpe/files\ to\ add\ to\ gvsoc/install_permanent.sh
```

See the [README inside that folder](files%20to%20add%20to%20gvsoc/) for
details on what gets patched and why.

To verify the setup works, build the toy_adder example:

```bash
cd /path/to/Pulp-Based-ML-Hardware-Acceleration   # repo root
source setup_env.sh
cd hwpe/toy_adder
make clean all run
```

---

## Step 1 — Scaffold your HWPE

Copy `toy_adder/` as a starting point:

```bash
cp -r hwpe/toy_adder hwpe/my_accel
cd hwpe/my_accel
```

Your directory will look like this:

```
my_accel/
├── archi_toy_adder.h       → rename to archi_my_accel.h
├── hal_toy_adder.h         → rename to hal_my_accel.h
├── toy_adder.c             → rename to my_accel.c
├── Makefile
├── inc/
│   ├── a_input.h           # test input data
│   ├── b_input.h
│   └── golden.h            # expected output (golden reference)
└── model/
    ├── toy_adder.py        → rename to my_accel.py
    └── toy_adder.cpp       → rename to my_accel.cpp
```

## Step 2 — Define the register map (`archi_*.h`)

The HWPE sits on the peripheral interconnect at base address `0x10201000`.
By convention, the register space is split into two regions:

| Region | Offset range | Purpose |
|--------|-------------|---------|
| **Control** | `0x00`–`0x14` | Standard HWPE protocol (TRIGGER, ACQUIRE, STATUS, FINISHED, SOFT_CLEAR) — keep these as-is. |
| **Job** | `0x40`+ | Your accelerator-specific configuration (data pointers, dimensions, modes, etc.). |

Edit `archi_my_accel.h` to add whatever job registers your accelerator needs.

## Step 3 — Write the HAL (`hal_*.h`)

Thin inline helpers that wrap `HWPE_WRITE` / `HWPE_READ` for each register.
The control helpers (`hwpe_trigger_job`, `hwpe_acquire`, `hwpe_soft_clear`, …)
can usually be reused verbatim from the toy_adder HAL — just rename the file
and update the include guard.

## Step 4 — Write the C++ simulation model (`model/<name>.cpp`)

This is where your accelerator logic lives.  The model must:

1. **Declare ports** — `input` (MMIO slave), `out` (L1 master), `irq` (wire).
2. **Handle MMIO** — dispatch register reads/writes for control and job regs.
3. **Implement a FSM** — on TRIGGER, read inputs from L1, compute, write
   results to L1, assert IRQ.

The toy_adder model covers all of this in ~200 lines and is heavily commented.
Key sections to adapt:

- **Job registers** — add fields for your configuration.
- **FSM body** — replace the `A + B` logic with your computation.
- **Memory accesses** — add more `access_mem()` calls if you read/write more
  data (e.g. vectors, matrices).

> Every model must end with an `extern "C" vp::Component *gv_new(…)` factory
> function — don't forget it.

## Step 5 — Write the Python wrapper (`model/<name>.py`)

Minimal boilerplate that tells GVSoC about the C++ source and the port names.
Copy `toy_adder.py` and change the `add_sources()` path.  The three port
methods (`i_INPUT`, `o_OUT`, `o_IRQ`) and their signatures should stay the same
unless you're adding extra ports.

## Step 6 — Write the test program (`<name>.c`)

The typical flow:

1. `pi_l1_malloc()` — allocate input and output buffers in L1.
2. Fill the input buffers with test data.
3. Program the HWPE via your HAL (set pointers, config registers).
4. `hwpe_trigger_job()` — kick off the accelerator.
5. `eu_evt_maskWaitAndClr(1 << 12)` — sleep until the HWPE IRQ fires.
6. Compare the output buffer against the golden reference.

## Step 7 — Update the Makefile

The critical line is the `--target-property` that tells GVSoC where your
Python wrapper lives:

```makefile
APP = my_accel
APP_SRCS += my_accel.c
APP_CFLAGS += -O3 -g -Iinc

override runner_args += --target-property=chip/cluster/custom_hwpe=$(realpath model/my_accel.py)

include $(RULES_DIR)/pmsis_rules.mk
```

## Step 8 — Compile the `.so` and run

The install scripts handle the toy_adder `.so` automatically, but for a **new**
model you need to compile it yourself.  See the
[compilation instructions](files%20to%20add%20to%20gvsoc/) or adapt the
one-liner:

```bash
GVSOC_ROOT=gvsoc
SRC=hwpe/my_accel/model/my_accel.cpp

SO_NAME=$(python3 -c "
import hashlib, os
src = os.path.abspath('$SRC')
h = int(hashlib.md5(src.encode()).hexdigest()[0:7], 16)
t = src.replace('/','_').replace('.','_')
print(f'gen_{t}_{h}.so')
")

g++ -O3 -std=gnu++17 -fPIC -shared -fno-stack-protector -D__GVSOC__ \
  -I"$GVSOC_ROOT/core/engine/include" \
  -I"$GVSOC_ROOT/core/models" \
  -I"$GVSOC_ROOT/pulp" \
  -I"$GVSOC_ROOT/pulp/targets" \
  -I"$GVSOC_ROOT/gvrun/python" \
  -I"$GVSOC_ROOT/build/core" \
  "$SRC" \
  -o "$GVSOC_ROOT/install/models/$SO_NAME"
```

Then build and run:

```bash
source setup_env.sh        # from repo root
cd hwpe/my_accel
make clean all run
```

---

## Reference: how the custom HWPE slot is wired

The patched cluster config adds a **string property** called `custom_hwpe`.
When a Makefile sets it to the path of a Python model wrapper, GVSoC
dynamically loads that class and wires it into the cluster:

| Connection | Detail |
|------------|--------|
| **MMIO**   | Mapped at `0x10201000` (size `0x400`) through the peripheral interconnect. The base address is stripped by `remove_offset` so the model sees offsets `0x00`–`0x3FF`. |
| **L1**     | The model's `out` master port goes through a `Router` that strips the L1 base (`0x10000000`), then into the shared interleaver port (`nb_pe + 4`). |
| **IRQ**    | The model's `irq` wire connects to the `acc_0` event line (index 12) on every PE's event unit. Software waits with `eu_evt_maskWaitAndClr(1 << 12)`. |

If `custom_hwpe` is left empty (the default), no HWPE is instantiated and the
cluster behaves identically to stock PULP-open.

## Directory structure

```
hwpe/
├── README.md                      # (this file)
├── files to add to gvsoc/         # Cluster config patches + install scripts
│   ├── cluster.py                 #   custom_hwpe property + dynamic loading
│   ├── cluster.json               #   MMIO address mapping (0x10201000)
│   ├── l1_subsystem.py            #   L1 Router for address stripping
│   ├── install_quick.sh           #   Option A installer
│   ├── install_permanent.sh       #   Option B installer
│   └── README.md
├── toy_adder/                     # Minimal working HWPE example (A + B)
│   ├── README.md
│   └── ...
└── examples/                      # Additional reference material
```

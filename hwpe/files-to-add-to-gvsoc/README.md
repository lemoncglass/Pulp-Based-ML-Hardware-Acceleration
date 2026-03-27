# Files to add to GVSoC

This directory contains the Python/JSON files that wire the **custom HWPE
slot** into the PULP-open cluster configuration, plus two install scripts
that **auto-scan** this folder to auto install what they can and warn you about what they can't.

| File               | What it does |
|--------------------|--------------|
| `cluster.py`       | Declares the `custom_hwpe` string property, dynamically loads the model `.py` at the path you give it, and wires the MMIO, IRQ, and L1 ports. |
| `cluster.json`     | Adds the peripheral address mapping for `custom_hwpe` (base `0x10201000`). |
| `l1_subsystem.py`  | Routes the HWPE's L1 master port through a `Router` that strips the L1 base address before reaching the interleaver. |

They replace the **originals** that ship with gvsoc-pulp.

---

## How the scripts work

Both scripts **auto-scan** this folder — you don't have to edit them when you
add or remove files.  They decide what to do based on file extension:

| Extension          | `install_quick.sh`      | `install_permanent.sh`        |
|--------------------|-------------------------|-------------------------------|
| `.py`, `.json`     | ✅ Copied to install tree | ✅ Copied to source tree       |
| `.c`, `.cpp`, `.h`, `.hpp` | ⚠ **Skipped** (warning printed) | ✅ Copied + incremental rebuild |
| `.md`, `.sh`       | Ignored                 | Ignored                       |

### Where files end up

- **Top-level `.py` / `.json`** → `gvsoc/pulp/pulp/chips/pulp_open/`
  (generator files).

- **C/C++ files in subdirectories** → mirrored into the gvsoc source tree.
  The subdirectory path tells the script where to put them.  For example:

  ```
  files-to-add-to-gvsoc/
    core/models/mymodel/foo.cpp      →  gvsoc/core/models/mymodel/foo.cpp
    pulp/pulp/chips/pulp_open/bar.h  →  gvsoc/pulp/pulp/chips/pulp_open/bar.h
  ```

**/!\ NOTE:**
- **Top-level C/C++ files** → **Skipped with a warning** (the script can't guess the destination). Move them into a subdirectory that mirrors the gvsoc path.

### Rebuild strategy (`install_permanent.sh`)

| What changed            | What the script does                         | Speed   |
|-------------------------|----------------------------------------------|---------|
| Only `.py` / `.json`    | `cmake --install` (no compilation)           | Instant    |
| Any `.c` / `.cpp` / `.h` / `.hpp` | `cmake --build -j16` + `cmake --install` | Incremental — only recompiles detected changes, **not** a nuclear `make clean all` |
- If changes aren't being correctly copied, manually copy the relevant files to their home in the gvsoc source tree and run:

``` bash
cd gvsoc && make clean all
```
This will rebuild **all** of gvsoc from your edited source tree. Can take a while depending on how many CPU cores you have.

---

## Quick-start (two paths, pick one)

Run from anywhere — the scripts find the repo root automatically.

### Option A — Fast path (no rebuild)

Copies `.py` / `.json` straight into the GVSoC **install** tree.
Instant, but gets overwritten if you later run `make clean all` in `gvsoc/`.

```bash
bash hwpe/files-to-add-to-gvsoc/install_quick.sh
```

### Option B — Permanent (survives gvsoc/ rebuild)

Copies into the GVSoC **source** tree, then rebuilds as needed.
Persists across rebuilds.

```bash
bash hwpe/files-to-add-to-gvsoc/install_permanent.sh
```

### `--compile` flag (both scripts)

Add `--compile` to also compile the C++ model in the **current directory's**
`model/` subfolder into a `.so`.  This works from any HWPE project directory:

```bash
cd hwpe/your_hwpe # replace "your_hwpe" with the desired hwpe directory
bash ../files-to-add-to-gvsoc/install_quick.sh --compile

# or permanently:
bash ../files-to-add-to-gvsoc/install_permanent.sh --compile
```

The script finds every `model/*.cpp` in your `$PWD`, computes the hash-based
`.so` name that GVSoC expects, and compiles it into `gvsoc/install/models/`.
Without `--compile`, no `.so` compilation is done.

> **Why two directories?**  GVSoC's CMake build copies the Python generators
> from the source tree (`gvsoc/pulp/pulp/chips/pulp_open/`) into the install
> tree (`gvsoc/install/generators/pulp/chips/pulp_open/`) during
> `cmake --install`.  At runtime, `gvrun` only reads from the **install** tree.
> Option A edits the install tree directly (fast, but gets overwritten by a
> rebuild).  Option B edits the source tree so it persists across rebuilds.

---

## Adding a new C/C++ source file

1. Create a subdirectory matching the desired path under `gvsoc/`:
   ```
   mkdir -p hwpe/files-to-add-to-gvsoc/core/models/mymodel
   ```
2. Place your `.c` / `.cpp` / `.h` / `.hpp` file in that subdirectory.
3. Run `install_permanent.sh` — it will copy the file and do an incremental
   `cmake --build`.

> **Note:** If the file is a brand-new source that isn't referenced by any
> existing `CMakeLists.txt`, you'll also need to update the relevant CMake
> file so the build system knows about it.

---

## Verifying everything works

```bash
cd /path/to/Pulp-Based-ML-Hardware-Acceleration   # <-- adjust this to your repo root

source setup_env.sh
cd hwpe/your_hwpe # replace "your_hwpe" with the desired hwpe directory
make clean all run
```

If you run the lemon_adder hwpe, you should see:

```
[lemon_adder] Operand A (L1) = 42
[lemon_adder] Operand B (L1) = 17
[lemon_adder] Result         = 59
[lemon_adder] Golden         = 59

<<<<<<<<<<>>>>>>>>>>
[lemon_adder] PASS

<<<<<<<<<<>>>>>>>>>>
```
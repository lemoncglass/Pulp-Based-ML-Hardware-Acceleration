#!/usr/bin/env bash
#
# install_permanent.sh — Permanent install (Option B)
#
# Copies the custom-HWPE generator files into the GVSoC *source* tree,
# runs `cmake --install` to propagate them to the install tree, and
# compiles the toy_adder C++ model into a .so.
#
# These changes survive a `make clean all` rebuild of GVSoC.
#
set -euo pipefail

# ── Locate repo root (relative to this script) ──────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

GVSOC_ROOT="$REPO_ROOT/gvsoc"
SOURCE_DST="$GVSOC_ROOT/pulp/pulp/chips/pulp_open"
MODELS_DIR="$GVSOC_ROOT/install/models"

# ── Sanity checks ───────────────────────────────────────────────────────
if [[ ! -d "$SOURCE_DST" ]]; then
    echo "ERROR: Source directory not found:"
    echo "       $SOURCE_DST"
    echo "       Is the gvsoc submodule checked out?"
    exit 1
fi

if [[ ! -d "$GVSOC_ROOT/build" ]]; then
    echo "ERROR: GVSoC build directory not found:"
    echo "       $GVSOC_ROOT/build"
    echo "       Have you built GVSoC at least once?  (cd gvsoc && make clean all)"
    exit 1
fi

# ── 1. Copy Python / JSON generator files into the source tree ──────────
echo "Copying generator files → source tree..."
cp "$SCRIPT_DIR/cluster.py"       "$SOURCE_DST/"
cp "$SCRIPT_DIR/cluster.json"     "$SOURCE_DST/"
cp "$SCRIPT_DIR/l1_subsystem.py"  "$SOURCE_DST/"
echo "  ✓ cluster.py, cluster.json, l1_subsystem.py"

# ── 2. Re-install (propagates source → install tree) ───────────────────
echo "Running cmake --install to propagate to install tree..."
cmake --install "$GVSOC_ROOT/build" > /dev/null
echo "  ✓ install tree updated"

# ── 3. Compile the toy_adder C++ model (.so) ───────────────────────────
MODEL_SRC="$REPO_ROOT/hwpe/toy_adder/model/toy_adder.cpp"

if [[ ! -f "$MODEL_SRC" ]]; then
    echo "WARNING: $MODEL_SRC not found — skipping .so compilation."
    echo "         Generator files were still installed."
    exit 0
fi

# Compute the hash-based .so name that GVSoC expects
SO_NAME=$(python3 -c "
import hashlib, os
src = os.path.abspath('$MODEL_SRC')
h = int(hashlib.md5(src.encode()).hexdigest()[0:7], 16)
t = src.replace('/','_').replace('.','_')
print(f'gen_{t}_{h}.so')
")

echo "Compiling toy_adder model → $SO_NAME ..."
g++ -O3 -std=gnu++17 -fPIC -shared -fno-stack-protector -D__GVSOC__ \
    -I"$GVSOC_ROOT/core/engine/include" \
    -I"$GVSOC_ROOT/core/models" \
    -I"$GVSOC_ROOT/pulp" \
    -I"$GVSOC_ROOT/pulp/targets" \
    -I"$GVSOC_ROOT/gvrun/python" \
    -I"$GVSOC_ROOT/build/core" \
    "$MODEL_SRC" \
    -o "$MODELS_DIR/$SO_NAME"
echo "  ✓ $MODELS_DIR/$SO_NAME"

echo ""
echo "Done!  You can now run:  cd hwpe/toy_adder && make clean all run"

#!/usr/bin/env bash
#
# compile_custom_hwpe.sh — Compile custom HWPE model(s) for GVSoC
#
# This script searches for all .cpp files in the model/ directory under the current working directory
# and compiles each one into a .so file that GVSoC expects, placing the output in the correct models directory.
#
# Usage:
#   Run this script from your HWPE project root (the directory containing model/).
#   The script does NOT patch gvsoc to accept custom HWPEs, please ensure patch_quick or patch_permanent have been run in this workspace first.
#
# Requirements:
#   - The gvsoc submodule must be checked out and built at least once (to create the install/models directory).
#   - Python 3 must be available for generating unique .so names.
#   - patch_quick or patch_permanent must have been run to ensure GVSoC can recognize custom HWPEs.
#   - The model/ directory must exist and contain at least one .cpp file.
#
set -euo pipefail

# --- Configuration ---
REPO_ROOT="$(git rev-parse --show-toplevel)"
# Set these paths if not already set in your environment
GVSOC_ROOT="$REPO_ROOT/gvsoc"
MODELS_DIR="$GVSOC_ROOT/install/models"

CALLER_DIR="$(pwd)"
model_dir="$CALLER_DIR/model"

# --- Compile all .cpp files in model/ ---
if [[ ! -d "$model_dir" ]]; then
    echo ""
    echo "⚠  No model/ directory found in:"
    echo "   $CALLER_DIR"
    exit 1
fi

cpp_files=()
while IFS= read -r -d '' f; do
    cpp_files+=("$f")
done < <(find "$model_dir" -maxdepth 1 -name '*.cpp' -print0)

if (( ${#cpp_files[@]} == 0 )); then
    echo ""
    echo "⚠  No .cpp files found in:"
    echo "   $model_dir"
    exit 1
fi

echo ""
for MODEL_SRC in "${cpp_files[@]}"; do
    SO_NAME=$(python3 -c "
import hashlib, os
src = os.path.abspath('$MODEL_SRC')
h = int(hashlib.md5(src.encode()).hexdigest()[0:7], 16)
t = src.replace('/','_').replace('.','_')
print(f'gen_{t}_{h}.so')
")
    echo "Compiling $(basename "$MODEL_SRC") → $SO_NAME ..."
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
done

echo ""
echo "Done!"
echo ">> If GVSoC has been patched, you can now run [ \"make clean all run\" ] in the directory containing your HWPE's Makefile. <<"

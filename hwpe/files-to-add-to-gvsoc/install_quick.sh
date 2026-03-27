#!/usr/bin/env bash
#
# install_quick.sh — Fast-path install (Option A)
#
# Scans this folder and copies .py / .json files directly into the GVSoC
# *install* tree.
#
# Changes take effect immediately — no GVSoC rebuild required.
# Caveat: a future `make clean all` in gvsoc/ will overwrite these files.
#         Use install_permanent.sh if you need them to survive rebuilds.
#
# NOTE: This script ONLY handles .py and .json files.  If .c / .cpp / .h /
#       .hpp files are present in this folder they will be SKIPPED with a
#       warning — use install_permanent.sh for those.
#
# Optional:  --compile
#   Searches the caller's current working directory for model/*.cpp and
#   compiles each one into the .so that GVSoC expects.
#
set -euo pipefail

# ── Parse flags ─────────────────────────────────────────────────────────
DO_COMPILE=false
for arg in "$@"; do
    case "$arg" in
        --compile) DO_COMPILE=true ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

# ── Locate repo root (relative to this script) ──────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CALLER_DIR="$(pwd)"

GVSOC_ROOT="$REPO_ROOT/gvsoc"
INSTALL_DST="$GVSOC_ROOT/install/generators/pulp/chips/pulp_open"
MODELS_DIR="$GVSOC_ROOT/install/models"

# ── Sanity checks ───────────────────────────────────────────────────────
if [[ ! -d "$INSTALL_DST" ]]; then
    echo "ERROR: Install directory not found:"
    echo "       $INSTALL_DST"
    echo "       Have you built GVSoC at least once?  (cd gvsoc && make clean all)"
    exit 1
fi

# ── 1. Auto-scan and copy .py / .json files ─────────────────────────────
echo "Scanning $SCRIPT_DIR for .py and .json files..."

copied=0
# Top-level .py and .json → install generators directory
while IFS= read -r -d '' f; do
    fname="$(basename "$f")"
    cp "$f" "$INSTALL_DST/"
    echo "  ✓ $fname → install tree"
    ((copied++))
done < <(find "$SCRIPT_DIR" -maxdepth 1 -type f \( -name '*.py' -o -name '*.json' \) -print0)

if (( copied == 0 )); then
    echo "  (no .py or .json files found)"
fi

# ── 2. Warn about C/C++ files that this script cannot handle ────────────
skipped_files=()
while IFS= read -r -d '' f; do
    skipped_files+=("$(basename "$f")")
done < <(find "$SCRIPT_DIR" -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0)

if (( ${#skipped_files[@]} > 0 )); then
    echo ""
    echo "⚠  WARNING: The following C/C++ files were SKIPPED (quick install"
    echo "   cannot handle compiled sources — use install_permanent.sh instead):"
    for sf in "${skipped_files[@]}"; do
        echo "     • $sf"
    done
fi

# ── 3. Compile model .so (only with --compile) ─────────────────────────
if ! $DO_COMPILE; then
    echo ""
    echo "ℹ  .so compilation skipped (pass --compile to also compile model/*.cpp in \$PWD)."
fi

if $DO_COMPILE; then
    # Find all .cpp files under model/ in the caller's working directory
    model_dir="$CALLER_DIR/model"
    if [[ ! -d "$model_dir" ]]; then
        echo ""
        echo "⚠  --compile was passed but no model/ directory found in:"
        echo "   $CALLER_DIR"
        echo "   Skipping .so compilation."
    else
        cpp_files=()
        while IFS= read -r -d '' f; do
            cpp_files+=("$f")
        done < <(find "$model_dir" -maxdepth 1 -name '*.cpp' -print0)

        if (( ${#cpp_files[@]} == 0 )); then
            echo ""
            echo "⚠  --compile was passed but no .cpp files found in:"
            echo "   $model_dir"
        else
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
        fi
    fi
fi

echo ""
echo "Done!"

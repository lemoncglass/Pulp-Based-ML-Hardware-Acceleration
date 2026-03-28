#!/usr/bin/env bash
#
# install_permanent.sh — Permanent install (Option B)
#
# Scans this folder and copies files into the GVSoC *source* tree so they
# survive rebuilds.
#
#   .py / .json  →  gvsoc/pulp/pulp/chips/pulp_open/  (generator files)
#   .c / .cpp / .h / .hpp  →  mirrors subdirectory structure under gvsoc/
#       e.g.  core/models/foo/bar.cpp  →  gvsoc/core/models/foo/bar.cpp
#
# Rebuild strategy:
#   • If only .py / .json changed  →  cmake --install  (fast, no compilation)
#   • If any .c / .cpp / .h / .hpp →  cmake --build + cmake --install
#     (incremental — only recompiles what actually changed, NOT a nuclear
#      "make clean all")
#
# Files skipped: .md, .sh (documentation / scripts in this folder)
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

# ── 1. Scan and classify files ──────────────────────────────────────────
echo "Scanning $SCRIPT_DIR for installable files..."

py_json_files=()
c_cpp_files=()
c_cpp_toplevel=()    # C/C++ at root level (ambiguous destination)
c_cpp_subdir=()      # C/C++ in subdirs (mirrors gvsoc source tree)

# Collect top-level .py and .json
while IFS= read -r -d '' f; do
    py_json_files+=("$f")
done < <(find "$SCRIPT_DIR" -maxdepth 1 -type f \( -name '*.py' -o -name '*.json' \) -print0)

# Collect all .c / .cpp / .h / .hpp (recursively)
while IFS= read -r -d '' f; do
    rel="${f#"$SCRIPT_DIR"/}"
    # Check if it's at the top level or in a subdirectory
    if [[ "$rel" == "$(basename "$rel")" ]]; then
        c_cpp_toplevel+=("$f")
    else
        c_cpp_subdir+=("$f")
    fi
    c_cpp_files+=("$f")
done < <(find "$SCRIPT_DIR" -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0)

needs_cmake_build=false

# ── 2. Copy .py / .json → source tree ───────────────────────────────────
if (( ${#py_json_files[@]} > 0 )); then
    echo ""
    echo "Copying generator files → source tree..."
    for f in "${py_json_files[@]}"; do
        fname="$(basename "$f")"
        cp "$f" "$SOURCE_DST/"
        echo "  ✓ $fname"
    done
else
    echo "  (no .py or .json files found)"
fi

# ── 3. Handle C/C++ files ───────────────────────────────────────────────
# Top-level C/C++ files: can't guess the destination
if (( ${#c_cpp_toplevel[@]} > 0 )); then
    echo ""
    echo "⚠  WARNING: The following C/C++ files are at the TOP LEVEL of this"
    echo "   folder — the script cannot determine their destination in the"
    echo "   GVSoC source tree.  Please move them into a subdirectory that"
    echo "   mirrors the gvsoc/ path.  For example:"
    echo ""
    echo "     files-to-add-to-gvsoc/core/models/mymodel/foo.cpp"
    echo "       → copied to gvsoc/core/models/mymodel/foo.cpp"
    echo ""
    for f in "${c_cpp_toplevel[@]}"; do
        echo "     SKIPPED: $(basename "$f")"
    done
fi

# Subdirectory C/C++ files: mirror into gvsoc source tree
if (( ${#c_cpp_subdir[@]} > 0 )); then
    echo ""
    echo "Copying C/C++ files → source tree (by subdirectory path)..."
    for f in "${c_cpp_subdir[@]}"; do
        rel="${f#"$SCRIPT_DIR"/}"
        dst="$GVSOC_ROOT/$rel"
        dst_dir="$(dirname "$dst")"
        mkdir -p "$dst_dir"
        cp "$f" "$dst"
        echo "  ✓ $rel"
    done
    needs_cmake_build=true
fi

# ── 4. Rebuild / reinstall ──────────────────────────────────────────────
echo ""
if $needs_cmake_build; then
    echo "C/C++ files detected → running incremental cmake --build + --install..."
    echo "  (This is NOT a nuclear rebuild — only changed files are recompiled.)"
    cmake --build "$GVSOC_ROOT/build" -j 16
    cmake --install "$GVSOC_ROOT/build" > /dev/null
    echo "  ✓ incremental build + install complete"
else
    echo "Only .py / .json files → running cmake --install (no compilation)..."
    cmake --install "$GVSOC_ROOT/build" > /dev/null
    echo "  ✓ install tree updated"
fi

# ── 5. Compile model .so (only with --compile) ─────────────────────────
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

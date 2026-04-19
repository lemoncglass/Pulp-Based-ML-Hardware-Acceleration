#!/usr/bin/env bash
#
# build_xlemon_target.sh — Build ALL custom XLemon target .so files
#
# This script:
#   1. Uses 'gvsoc ... components' to discover the exact .so names GVSoC expects
#   2. Compiles only the components that are missing from gvsoc/install/models/
#   3. Stock GVSoC components (memory, router) are already built by setup.sh.
#      This script builds the xlemon-specific ones (ISS core, XifDecoder,
#      XLemonAdder, ExitModule).
#
# Usage:
#   cd hwpe/xlemon_adder
#   ./build_xlemon_target.sh
#
# Prerequisites:
#   - source setup_env.sh (or at least gvsoc/sourceme.sh)
#   - GVSoC must be built (gvsoc/install/ must exist)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
GVSOC_ROOT="$REPO_ROOT/gvsoc"
MODELS_DIR="$GVSOC_ROOT/install/models"
GENERATORS_DIR="$GVSOC_ROOT/install/generators"
CHIP_DIR="$SCRIPT_DIR/chip"

BUILD_DIR="$SCRIPT_DIR/BUILD/gvsoc"
OBJ_DIR="$BUILD_DIR/obj"
mkdir -p "$BUILD_DIR" "$OBJ_DIR"

# Source search paths for resolving relative source file references
SRC_SEARCH_DIRS=(
    "$GENERATORS_DIR"
    "$GVSOC_ROOT/core/models"
    "$GVSOC_ROOT/pulp"
    "$BUILD_DIR"
)

# Compiler settings
GVSOC_INC="$GVSOC_ROOT/core/engine/include"
GVSOC_MODELS="$GVSOC_ROOT/core/models"
GVSOC_BUILD="$GVSOC_ROOT/build/core"
PULP_DIR="$GVSOC_ROOT/pulp"
CC_INCS="-I$GVSOC_INC -I$GVSOC_MODELS -I$PULP_DIR -I$GVSOC_BUILD -I$BUILD_DIR -I$CHIP_DIR"
CC_COMMON="-O2 -fPIC -fno-stack-protector -D__GVSOC__"

echo ""
echo "=== Building XLemon Target Components ==="
echo ""

# --- Step 1: Discover component names ---
echo "[1/4] Discovering component names..."
COMP_CFG="$BUILD_DIR/components.cfg"
gvsoc \
    --target-dir="$CHIP_DIR" \
    --target=xlemon_target \
    --binary=/dev/null \
    --builddir="$BUILD_DIR" \
    --installdir="$MODELS_DIR" \
    --component-file="$COMP_CFG" \
    components 2>/dev/null

if [ ! -f "$COMP_CFG" ]; then
    echo "  ✗ Failed to generate components.cfg"
    exit 1
fi
echo "  ✓ Generated components.cfg"

# --- Step 2: Parse components.cfg ---
echo "[2/4] Parsing component list..."
declare -A COMP_SRCS
declare -A COMP_CFLAGS

while IFS='=' read -r key value; do
    if [[ "$key" == CONFIG_SRCS_gen_* ]]; then
        COMP_SRCS["${key#CONFIG_SRCS_}"]="$value"
    elif [[ "$key" == CONFIG_CFLAGS_gen_* ]]; then
        COMP_CFLAGS["${key#CONFIG_CFLAGS_}"]="$value"
    fi
done < "$COMP_CFG"

echo "  Found ${#COMP_SRCS[@]} total components"

# Helper: resolve a source file path
resolve_src() {
    local src="$1"
    if [ -f "$src" ]; then echo "$src"; return 0; fi
    for dir in "${SRC_SEARCH_DIRS[@]}"; do
        if [ -f "$dir/$src" ]; then echo "$dir/$src"; return 0; fi
    done
    return 1
}

# --- Step 3: Compile missing components ---
echo "[3/4] Compiling missing components..."

COMPILED=0
SKIPPED=0
FAILED=0

for comp_name in "${!COMP_SRCS[@]}"; do
    so_path="$MODELS_DIR/${comp_name}.so"

    # Skip if .so already exists
    if [ -f "$so_path" ]; then
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    srcs="${COMP_SRCS[$comp_name]}"
    cflags="${COMP_CFLAGS[$comp_name]:-}"

    echo "  Building: $comp_name"

    # Resolve all source paths and separate .c from .cpp
    c_files=()
    cpp_files=()
    all_found=true
    for src in $srcs; do
        resolved=$(resolve_src "$src") || {
            echo "    ✗ Source not found: $src"
            all_found=false
            break
        }
        if [[ "$resolved" == *.c ]]; then
            c_files+=("$resolved")
        else
            cpp_files+=("$resolved")
        fi
    done

    if ! $all_found; then
        FAILED=$((FAILED + 1))
        continue
    fi

    # If there are .c files, we need separate compilation + linking
    # (.c files like flexfloat.c use 'try' as a variable name, which is
    # a keyword in C++ — they must be compiled with gcc, not g++)
    if [ ${#c_files[@]} -gt 0 ]; then
        obj_files=()

        # Compile .c files with gcc
        for src in "${c_files[@]}"; do
            obj="$OBJ_DIR/$(basename "$src" .c).o"
            echo "    [gcc] $(basename $src)"
            gcc $CC_COMMON $CC_INCS $cflags -c "$src" -o "$obj" || {
                echo "    ✗ gcc failed on $src"
                FAILED=$((FAILED + 1))
                continue 2
            }
            obj_files+=("$obj")
        done

        # Compile .cpp files with g++
        for src in "${cpp_files[@]}"; do
            obj="$OBJ_DIR/$(basename "$src" .cpp).o"
            echo "    [g++] $(basename $src)"
            g++ -std=gnu++17 $CC_COMMON $CC_INCS $cflags -c "$src" -o "$obj" || {
                echo "    ✗ g++ failed on $src"
                FAILED=$((FAILED + 1))
                continue 2
            }
            obj_files+=("$obj")
        done

        # Link
        echo "    [link] → $(basename $so_path)"
        g++ -shared "${obj_files[@]}" -o "$so_path" || {
            echo "    ✗ Linking failed"
            FAILED=$((FAILED + 1))
            continue
        }
    else
        # Pure C++ — compile + link in one step
        if ! g++ -std=gnu++17 $CC_COMMON -shared $CC_INCS $cflags "${cpp_files[@]}" -o "$so_path" 2>&1; then
            echo "    ✗ Compilation failed"
            FAILED=$((FAILED + 1))
            continue
        fi
    fi

    echo "    ✓ $(basename $so_path)"
    COMPILED=$((COMPILED + 1))
done

# --- Summary ---
echo ""
echo "[4/4] Summary"
echo "  Compiled: $COMPILED  |  Already present: $SKIPPED  |  Failed: $FAILED"
if [ $FAILED -gt 0 ]; then
    echo "  ✗ $FAILED component(s) failed"
    exit 1
fi
echo ""
echo "  All .so files are in: $MODELS_DIR"
echo "  Run the simulation with: make sim"
echo ""

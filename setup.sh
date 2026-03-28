#!/bin/bash
# setup.sh — PULP ML Hardware Acceleration full environment setup
#
# Usage:
#   ./setup.sh              Incremental (skip already-built steps)
#   ./setup.sh --aggressive Full rebuild of every component

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ─── Flags ───────────────────────────────────────────────────────────────────
AGGRESSIVE=0
for arg in "$@"; do
  case "$arg" in
    --aggressive) AGGRESSIVE=1 ;;
    -h|--help)
      echo "Usage: $0 [--aggressive]"
      echo "  --aggressive   Force a full rebuild of every component"
      exit 0 ;;
    *)
      echo "Unknown option: $arg (try --help)"
      exit 1 ;;
  esac
done

# ─── Helpers ─────────────────────────────────────────────────────────────────
# ask_continue — called when a step fails.
#   Prints the error, then asks the user whether to skip or abort.
ask_continue() {
  local section="$1"
  local rc="$2"
  echo ""
  echo "!!! Section \"$section\" failed (exit code $rc)."
  # If stdin is not a terminal (e.g. piped), default to skip
  if [ ! -t 0 ]; then
    echo "    (non-interactive) Skipping and continuing..."
    return 0
  fi
  while true; do
    read -rp "    Skip this section and continue? [y/n] " yn
    case "$yn" in
      [Yy]*) echo "    Skipping \"$section\"."; return 0 ;;
      [Nn]*) echo "    Aborting."; exit "$rc" ;;
      *)     echo "    Please answer y or n." ;;
    esac
  done
}

# run_section — wrapper that runs a function and catches failures.
#   Usage: run_section "Section Name" function_name
run_section() {
  local name="$1"
  local func="$2"
  echo ""
  echo "─── $name ────────────────────────────────────────────────"
  if "$func"; then
    echo ">>> $name — done."
  else
    ask_continue "$name" "$?"
  fi
  cd "$SCRIPT_DIR"
}

# ─── Section functions ───────────────────────────────────────────────────────

section_system_deps() {
  echo ">>> Installing system packages..."
  sudo apt-get update
  sudo apt-get install -y \
    build-essential git libftdi-dev libftdi1 doxygen python3-pip \
    libsdl2-dev curl cmake libusb-1.0-0-dev scons gtkterm \
    libsndfile1-dev rsync autoconf automake texinfo libtool \
    pkg-config libsdl2-ttf-dev

  # Detect Ubuntu 24.04+ (PEP 668 requires --break-system-packages)
  PIP_EXTRA=""
  if python3 -c "import sysconfig; exit(0 if sysconfig.get_path('stdlib').find('EXTERNALLY-MANAGED') else 1)" 2>/dev/null || \
     [ -f /usr/lib/python3.*/EXTERNALLY-MANAGED ]; then
    PIP_EXTRA="--break-system-packages"
  fi
  pip3 install --user $PIP_EXTRA argcomplete pyelftools six pyyaml tabulate
}

section_toolchain() {
  # Clone if missing
  if [ ! -d pulp-riscv-gnu-toolchain ]; then
    echo ">>> Cloning PULP RISC-V toolchain..."
    git clone --recursive https://github.com/pulp-platform/pulp-riscv-gnu-toolchain
  fi

  # Skip the expensive build when the compiler already exists (incremental mode)
  if [ "$AGGRESSIVE" -eq 0 ] && command -v "$HOME/riscv/bin/riscv32-unknown-elf-gcc" &>/dev/null; then
    echo ">>> Toolchain already built — skipping (use --aggressive to force rebuild)."
    export PATH="$HOME/riscv/bin:$PATH"
    return 0
  fi

  # Apply missing-file fixes (PULP fork is missing these — build will fail without them)
  echo ">>> Applying toolchain patches..."
  cp patches/sysroff.info \
     pulp-riscv-gnu-toolchain/riscv-binutils-gdb/binutils/sysroff.info
  mkdir -p pulp-riscv-gnu-toolchain/riscv-newlib/newlib/libc/include/sys
  cp patches/newlib-sys-config.h \
     pulp-riscv-gnu-toolchain/riscv-newlib/newlib/libc/include/sys/config.h

  echo ">>> Building toolchain (this is the slow part — ~30-60 min)..."
  cd pulp-riscv-gnu-toolchain
  ./configure --prefix="$HOME/riscv" --with-arch=rv32imc --with-cmodel=medlow --enable-multilib
  make newlib
  cd "$SCRIPT_DIR"

  export PATH="$HOME/riscv/bin:$PATH"
  echo ">>> Toolchain installed: $(riscv32-unknown-elf-gcc --version | head -1)"
}

section_pulp_sdk() {
  if [ ! -d pulp-sdk ]; then
    echo ">>> Cloning PULP SDK..."
    git clone https://github.com/pulp-platform/pulp-sdk
  fi

  # Skip build when the SDK build marker exists (incremental mode)
  if [ "$AGGRESSIVE" -eq 0 ] && [ -f pulp-sdk/build/pulp-open/config.mk ]; then
    echo ">>> PULP SDK already built — skipping (use --aggressive to force rebuild)."
    return 0
  fi

  cd pulp-sdk
  source configs/pulp-open.sh
  make build
}

section_gvsoc() {
  if [ ! -d gvsoc ]; then
    echo ">>> Cloning GVSoC simulator..."
    git clone https://github.com/gvsoc/gvsoc.git
    cd gvsoc
    git submodule update --init --recursive
    cd "$SCRIPT_DIR"
  fi

  # Skip build when install artifacts exist (incremental mode)
  if [ "$AGGRESSIVE" -eq 0 ] && [ -d gvsoc/install/lib ] && [ -d gvsoc/install/models ]; then
    echo ">>> GVSoC already built — skipping (use --aggressive to force rebuild)."
    return 0
  fi

  cd gvsoc
  pip3 install --user $PIP_EXTRA -r core/requirements.txt
  pip3 install --user $PIP_EXTRA -r gapy/requirements.txt
  make all TARGETS=pulp-open
}

section_ml_libs() {
  echo ">>> Cloning ML libraries..."
  if [ ! -d pulp-nn ]; then
    git clone https://github.com/pulp-platform/pulp-nn
  else
    echo "    pulp-nn already present."
  fi

  if [ ! -d pulp-train ]; then
    git clone https://github.com/pulp-platform/pulp-trainlib.git pulp-train
  else
    echo "    pulp-train already present."
  fi
}

section_redmule() {
  # README
  REDMULE_README="hwpe/examples/redmule/RedMulE_README.md"
  if [ "$AGGRESSIVE" -eq 1 ] || [ ! -f "$REDMULE_README" ]; then
    echo ">>> Downloading RedMulE README for reference..."
    curl -fsSL https://raw.githubusercontent.com/pulp-platform/redmule/main/README.md \
      -o "$REDMULE_README"
  else
    echo "    RedMulE README already present."
  fi

  # Architecture diagrams
  REDMULE_DOC_DIR="hwpe/examples/redmule/doc"
  if [ "$AGGRESSIVE" -eq 1 ] || [ ! -d "$REDMULE_DOC_DIR" ]; then
    echo ">>> Downloading RedMulE architecture diagrams..."
    mkdir -p "$REDMULE_DOC_DIR"
    curl -fsSL https://raw.githubusercontent.com/pulp-platform/redmule/main/doc/redmule_overview.png \
      -o "$REDMULE_DOC_DIR/redmule_overview.png"
    curl -fsSL https://raw.githubusercontent.com/pulp-platform/redmule/main/doc/RedmuleSubsystem-CoreComplex.png \
      -o "$REDMULE_DOC_DIR/RedmuleSubsystem-CoreComplex.png"
  else
    echo "    RedMulE diagrams already present."
  fi

  # Golden model
  GOLDEN_DIR="hwpe/examples/redmule/gemm/inc/test_header_generation/redmule-golden-model"
  if [ ! -d "$GOLDEN_DIR" ]; then
    echo ">>> Cloning RedMulE golden model..."
    git clone https://github.com/yvantor/redmule-golden-model.git "$GOLDEN_DIR"
  else
    echo "    RedMulE golden model already present."
  fi
}

# ─── Main ────────────────────────────────────────────────────────────────────
echo "=== PULP ML Hardware Acceleration — Full Setup ==="
if [ "$AGGRESSIVE" -eq 1 ]; then
  echo "Mode: --aggressive (full rebuild)"
else
  echo "Mode: incremental (skip already-built steps)"
fi
echo ""

run_section "1. System Dependencies"    section_system_deps
run_section "2. RISC-V Toolchain"       section_toolchain
run_section "3. PULP SDK"               section_pulp_sdk
run_section "4. GVSoC Simulator"        section_gvsoc
run_section "5. ML Libraries"           section_ml_libs
run_section "6. RedMulE & Golden Model" section_redmule

# ─── Done ────────────────────────────────────────────────────────────────────
echo ""
echo "============================================="
echo "  Setup complete!"
echo "  Run:  source setup_env.sh"
echo "  Test: cd pulp-sdk/tests/hello && make clean all run"
echo "============================================="
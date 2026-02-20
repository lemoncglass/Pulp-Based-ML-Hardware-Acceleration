#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== PULP ML Hardware Acceleration — Full Setup ==="
echo "This will take 30-60+ minutes (toolchain compilation)."
echo ""

# ─── 1. System Dependencies ─────────────────────────────────────────────────
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

# ─── 2. RISC-V Toolchain ────────────────────────────────────────────────────
echo ">>> Cloning PULP RISC-V toolchain..."
if [ ! -d pulp-riscv-gnu-toolchain ]; then
  git clone --recursive https://github.com/pulp-platform/pulp-riscv-gnu-toolchain
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

# ─── 3. PULP SDK ────────────────────────────────────────────────────────────
echo ">>> Cloning PULP SDK..."
if [ ! -d pulp-sdk ]; then
  git clone https://github.com/pulp-platform/pulp-sdk
fi
cd pulp-sdk
source configs/pulp-open.sh
make build
cd "$SCRIPT_DIR"

# ─── 4. GVSoC Simulator ─────────────────────────────────────────────────────
echo ">>> Cloning and building GVSoC simulator..."
if [ ! -d gvsoc ]; then
  git clone https://github.com/gvsoc/gvsoc.git
  cd gvsoc
  git submodule update --init --recursive
else
  cd gvsoc
fi
pip3 install --user $PIP_EXTRA -r core/requirements.txt
pip3 install --user $PIP_EXTRA -r gapy/requirements.txt
make all TARGETS=pulp-open
cd "$SCRIPT_DIR"

# ─── 5. ML Libraries ────────────────────────────────────────────────────────
echo ">>> Cloning ML libraries..."
[ ! -d pulp-nn ]    && git clone https://github.com/pulp-platform/pulp-nn
[ ! -d pulp-train ] && git clone https://github.com/pulp-platform/pulp-train

# ─── Done ────────────────────────────────────────────────────────────────────
echo ""
echo "============================================="
echo "  Setup complete!"
echo "  Run:  source setup_env.sh"
echo "  Test: cd pulp-sdk/tests/hello && make clean all run"
echo "============================================="
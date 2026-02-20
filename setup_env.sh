#!/bin/bash

export PULP_RISCV_GCC_TOOLCHAIN=$HOME/riscv
export PATH=$PULP_RISCV_GCC_TOOLCHAIN/bin:$PATH

# Source GVSoC simulator environment
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/gvsoc/sourceme.sh" ]; then
    source "$SCRIPT_DIR/gvsoc/sourceme.sh"
fi

cd pulp-sdk
source configs/pulp-open.sh
cd ..
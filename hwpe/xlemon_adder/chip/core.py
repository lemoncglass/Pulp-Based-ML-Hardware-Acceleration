#
# XLemon CV32 Core — Core definition with xlemon ISA extension
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 University of Missouri - Kansas City
#
# Based on Magia's cv32/core.py.  Constructs a RV32IMAFC core
# with the Rv32xlemon ISA extension (xladd instruction).
#
# The key difference from a plain rv32 core:
#   1. The ISA includes Rv32xlemon() so the decoder knows about xladd
#   2. CONFIG_ISS_CORE_DIR points to the chip/ directory so the ISS
#      can find xlemon_adder_xif.hpp (the xladd_exec handler)
#   3. The core inherits o_OFFLOAD() and i_OFFLOAD_GRANT() from
#      RiscvCommon — these are the XIF offload/grant wire ports.
#

import gvsoc.systree
import cpu.iss.riscv
import cpu.iss.isa_gen.isa_riscv_gen
import os

# Import our custom ISA subset
import sys
_CHIP_DIR = os.path.dirname(os.path.abspath(__file__))
if _CHIP_DIR not in sys.path:
    sys.path.insert(0, _CHIP_DIR)
from isa import Rv32xlemon


class XLemonCore(cpu.iss.riscv.RiscvCommon):
    def __init__(self, parent, name, binaries=[], fetch_enable=False,
                 boot_addr=0, timed=True, core_id=0):

        isa_str = 'rv32imafc'
        misa = 0x40000000
        debug_handler = 0x1a190800
        riscv_exceptions = True

        # Build the ISA object with our custom xlemon extension.
        # This is the same pattern as Magia's core.py, but with
        # Rv32xlemon() instead of Rv32redmule(), iDMA_Ctrl(), FSync().
        isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(
            'xlemon-cv32', isa_str,
            extensions=[Rv32xlemon()]
        )

        super().__init__(parent, name, isa=isa, misa=misa, core_id=core_id,
                         debug_handler=debug_handler, fetch_enable=fetch_enable,
                         riscv_exceptions=riscv_exceptions)

        # Tell the ISS where to find our custom instruction handler.
        # The ISS will look for xlemon_adder_xif.hpp relative to this path.
        self.add_c_flags([
            '-DPIPELINE_STALL_THRESHOLD=1',
            f'-DCONFIG_ISS_CORE_DIR={_CHIP_DIR}',
        ])

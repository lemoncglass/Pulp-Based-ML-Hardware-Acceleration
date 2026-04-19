#
# XLemon Tile — Single-core tile with XIF-based XLemonAdder
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 University of Missouri - Kansas City
#
# Simplified version of Magia's tile.py.  Contains:
#   - XLemonCore (CV32-style core with xlemon ISA + XIF ports)
#   - XLemonXifDecoder (1-slot opcode router)
#   - XLemonAdder (the accelerator)
#   - L1 TCDM (banked scratchpad memory)
#   - UART stdout
#
# No iDMA, no FractalSync, no multi-tile interconnect.
#
# XIF wiring:
#   core.o_OFFLOAD() → xifdec.i_OFFLOAD_M()
#   xifdec.o_OFFLOAD_GRANT_M() → core.i_OFFLOAD_GRANT()
#   xifdec.o_OFFLOAD_S1() → xlemon.i_OFFLOAD()
#   xlemon.o_OFFLOAD_GRANT() → xifdec.i_OFFLOAD_GRANT_S1()
#   xlemon.o_OUT() → L1 TCDM
#   xlemon.o_IRQ() → core IRQ
#

import gvsoc.systree
import memory.memory as memory
import interco.router as router
import utils.loader.loader
import gdbserver.gdbserver
from pulp.stdout.stdout_v3 import Stdout

import os
import sys

_CHIP_DIR = os.path.dirname(os.path.abspath(__file__))
_MODEL_DIR = os.path.join(os.path.dirname(_CHIP_DIR), 'model')

# Import our local components
if _CHIP_DIR not in sys.path:
    sys.path.insert(0, _CHIP_DIR)
if _MODEL_DIR not in sys.path:
    sys.path.insert(0, _MODEL_DIR)

from core import XLemonCore
from xif_decoder import XLemonXifDecoder
from xlemon_adder import XLemonAdder
from exit_module import ExitModule


class XLemonTile(gvsoc.systree.Component):
    """A minimal single-core tile for testing the XLemonAdder HWPE.

    Memory map:
      0x1000_0000 - 0x1001_FFFF : L1 TCDM (128 KB, 16 banks × 8 KB)
      0xFFFF_0004 - 0xFFFF_0104 : UART stdout
      0x1A19_0800               : Debug handler
    """

    # Memory map constants
    L1_BASE = 0x1000_0000
    L1_SIZE = 0x0002_0000   # 128 KB
    L1_NB_BANKS = 16
    L1_BANK_SIZE = L1_SIZE // L1_NB_BANKS
    L2_BASE = 0x1C00_0000
    L2_SIZE = 0x0008_0000   # 512 KB
    STDOUT_BASE = 0x1A10_F000
    STDOUT_SIZE = 0x1000
    EXIT_BASE = 0x1A10_E000
    EXIT_SIZE = 0x1000

    def __init__(self, parent, name, parser, binary=None):
        super().__init__(parent, name)

        # ---- Instantiate components ----
        core = XLemonCore(self, 'core', core_id=0)

        # L1 TCDM — simple banked memory behind a router/interleaver
        l1_ico = router.Router(self, 'l1_ico', bandwidth=4)
        for i in range(self.L1_NB_BANKS):
            bank = memory.Memory(self, f'l1_bank_{i}', size=self.L1_BANK_SIZE,
                                 atomics=True)
            l1_ico.add_mapping(f'bank_{i}',
                               base=self.L1_BASE + i * self.L1_BANK_SIZE,
                               remove_offset=self.L1_BASE + i * self.L1_BANK_SIZE,
                               size=self.L1_BANK_SIZE)
            self.bind(l1_ico, f'bank_{i}', bank, 'input')

        # Data interconnect — routes core data accesses
        data_ico = router.Router(self, 'data_ico', bandwidth=4)
        data_ico.add_mapping('l1', base=self.L1_BASE, size=self.L1_SIZE)
        self.bind(data_ico, 'l1', l1_ico, 'input')

        # L2 memory — for code and data (matches pulp-open linker script)
        l2 = memory.Memory(self, 'l2', size=self.L2_SIZE)
        data_ico.add_mapping('l2', base=self.L2_BASE, size=self.L2_SIZE,
                             remove_offset=self.L2_BASE)
        self.bind(data_ico, 'l2', l2, 'input')

        # Low address range for data_tiny_fc (.data_tiny_fc at 0x4)
        low_mem = memory.Memory(self, 'low_mem', size=0x10000)
        data_ico.add_mapping('low_mem', base=0x0, size=0x10000)
        self.bind(data_ico, 'low_mem', low_mem, 'input')

        # XIF Decoder
        xifdec = XLemonXifDecoder(self, 'xifdec')

        # XLemonAdder accelerator
        xlemon = XLemonAdder(self, 'xlemon')

        # UART stdout
        stdout = Stdout(self, 'stdout', max_cluster=1, max_core_per_cluster=1,
                        user_set_core_id=0, user_set_cluster_id=0)
        data_ico.add_mapping('stdout', base=self.STDOUT_BASE, size=self.STDOUT_SIZE)
        self.bind(data_ico, 'stdout', stdout, 'input')

        # Exit module — write to this address to terminate the simulation
        exit_mod = ExitModule(self, 'exit_mod')
        data_ico.add_mapping('exit', base=self.EXIT_BASE, size=self.EXIT_SIZE)
        self.bind(data_ico, 'exit', exit_mod, 'input')

        # ELF loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # ---- Core data/fetch → interconnect ----
        core.o_DATA(data_ico.i_INPUT())
        core.o_DATA_DEBUG(data_ico.i_INPUT())
        core.o_FETCH(data_ico.i_INPUT())

        # ---- Loader → interconnect ----
        self.bind(loader, 'out', data_ico, 'input')
        self.bind(loader, 'start', core, 'fetchen')
        self.bind(loader, 'entry', core, 'bootaddr')

        # ============================================================
        #  XIF WIRING — The core of the XIF pipeline
        # ============================================================

        # Core → XifDecoder (core fires unknown opcodes)
        core.o_OFFLOAD(xifdec.i_OFFLOAD_M())

        # XifDecoder → Core (grant/unstall)
        xifdec.o_OFFLOAD_GRANT_M(core.i_OFFLOAD_GRANT())

        # XifDecoder → XLemonAdder (route xladd opcode)
        xifdec.o_OFFLOAD_S1(xlemon.i_OFFLOAD())

        # XLemonAdder → XifDecoder (grant back)
        xlemon.o_OFFLOAD_GRANT(xifdec.i_OFFLOAD_GRANT_S1())

        # ============================================================
        #  Accelerator memory + IRQ
        # ============================================================

        # XLemonAdder → L1 TCDM (read/write operands)
        xlemon.o_OUT(l1_ico.i_INPUT())

        # XLemonAdder → Core IRQ line 12 (XLEMON_ADDER_EVT0)
        xlemon.o_IRQ(core.i_IRQ(12))

        # Debug (disabled — was blocking reset?)
        # gdbserver.gdbserver.Gdbserver(self, 'gdbserver')

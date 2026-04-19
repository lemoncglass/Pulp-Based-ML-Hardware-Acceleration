#
# XLemon Board — Top-level target for GVSoC
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 University of Missouri - Kansas City
#
# This is the entry point that GVSoC uses when you run:
#   gvsoc --target=xlemon_adder ...
#
# It creates a clock domain and instantiates the XLemonTile.
#

import gvsoc.systree
import gvsoc.runner
import os
import sys

from vp.clock_domain import Clock_domain

_CHIP_DIR = os.path.dirname(os.path.abspath(__file__))
if _CHIP_DIR not in sys.path:
    sys.path.insert(0, _CHIP_DIR)

from tile import XLemonTile


class XLemonBoard(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, options=options)

        # Parse binary argument
        binary = None
        if parser is not None:
            [args, _] = parser.parse_known_args()
            binary = args.binary

        # Clock domain — 50 MHz (same as rv32 target)
        clock = Clock_domain(self, 'clock', frequency=50000000)

        # The tile
        tile = XLemonTile(self, 'tile', parser, binary=binary)

        # Connect clock
        self.bind(clock, 'out', tile, 'clock')


class Target(gvsoc.runner.Target):
    gapy_description = "XLemon Adder test board (single CV32 core + XIF + XLemonAdder)"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options, model=XLemonBoard)

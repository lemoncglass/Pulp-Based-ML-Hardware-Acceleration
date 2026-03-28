# Lemon Adder HWPE — GVSoC Python wrapper (skeleton)
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Cody Glassbrenner
#
# Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent) 
#
# This tells GVSoC:
#   1. Where the C++ source lives     (add_sources)
#   2. What ports the model exposes   (i_INPUT, o_OUT, o_IRQ)
#
# The cluster.py target config imports this class, instantiates it,
# and wires the ports to the cluster interconnect + event unit.
#

import gvsoc.systree
import os

# Absolute path to this model's directory — used to locate the C++ source
# regardless of the working directory GVSoC is invoked from.
_MODEL_DIR = os.path.dirname(os.path.abspath(__file__))


class LemonAdder(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)

        # Tell GVSoC to JIT-compile our C++ model from source.
        # Path is derived from __file__ so it works no matter where
        # the build is invoked or how the model is loaded.
        self.add_sources([os.path.join(_MODEL_DIR, 'lemon_adder.cpp')])

    # --- Slave port: MMIO register access from the CPU ---
    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    # --- Master port: memory read/write into L1 TCDM ---
    def o_OUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('out', itf, signature='io')

    # --- Master port: interrupt line to the event unit ---
    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq', itf, signature='wire<bool>')

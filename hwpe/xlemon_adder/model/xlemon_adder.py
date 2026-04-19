# XLemon Adder HWPE — GVSoC Python wrapper (XIF-only)
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 University of Missouri - Kansas City
#
# Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
#
# This tells GVSoC:
#   1. Where the C++ source lives       (add_sources)
#   2. What ports the model exposes      (XIF offload, memory, IRQ)
#
# === XIF vs MMIO port differences ===
#
# The MMIO lemon_adder had:
#   i_INPUT()  — IoSlave for memory-mapped register access
#
# This XIF xlemon_adder replaces that with:
#   i_OFFLOAD()        — WireSlave to receive custom instructions from XifDecoder
#   o_OFFLOAD_GRANT()  — WireMaster to send grant/result back through XifDecoder
#
# The memory (o_OUT) and IRQ (o_IRQ) ports remain unchanged — the
# accelerator still reads/writes L1 TCDM and fires interrupts the same way.
#
# === Wiring (in a Magia-style tile.py) ===
#
#   xifdec.o_OFFLOAD_Sn(xlemon.i_OFFLOAD())            # XifDecoder → us
#   xlemon.o_OFFLOAD_GRANT(xifdec.i_OFFLOAD_GRANT_Sn()) # us → XifDecoder
#   xlemon.o_OUT(l1_tcdm.i_INPUT())                     # us → TCDM
#   xlemon.o_IRQ(core.i_IRQ(n))                          # us → core IRQ
#

import gvsoc.systree
import os

_MODEL_DIR = os.path.dirname(os.path.abspath(__file__))


class XLemonAdder(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)

        self.add_sources([os.path.join(_MODEL_DIR, 'xlemon_adder.cpp')])

    # --- XIF offload slave: receives custom instructions from XifDecoder ---
    # The XifDecoder routes instructions with our opcode to this port.
    # Signature must match the C++ side: wire<IssOffloadInsn<uint32_t>*>
    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    # --- XIF grant master: sends grant/result back to core via XifDecoder ---
    # When the accelerator needs to un-stall the core or write back a result,
    # it syncs on this wire.  The XifDecoder forwards it to the core.
    def o_OFFLOAD_GRANT(self, itf):
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    # --- Master port: memory read/write into L1 TCDM ---
    # (unchanged from MMIO version — accelerators always need memory access)
    def o_OUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('out', itf, signature='io')

    # --- Master port: interrupt line ---
    # (unchanged from MMIO version — fires when computation finishes)
    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq', itf, signature='wire<bool>')

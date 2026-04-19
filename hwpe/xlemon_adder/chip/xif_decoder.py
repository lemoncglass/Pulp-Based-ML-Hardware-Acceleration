#
# XLemon XIF Decoder — Python Wrapper
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 University of Missouri - Kansas City
#
# Simplified XifDecoder with 1 slave slot for the XLemonAdder.
# No FractalSync, no iDMA.
#

import gvsoc.systree
import os

_CHIP_DIR = os.path.dirname(os.path.abspath(__file__))


class XLemonXifDecoder(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.add_sources([os.path.join(_CHIP_DIR, 'xif_decoder.cpp')])

    # ---- Core-facing ports ----
    def i_OFFLOAD_M(self):
        return gvsoc.systree.SlaveItf(self, 'offload_m',
            signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT_M(self, itf):
        self.itf_bind('offload_grant_m', itf,
            signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    # ---- XLemonAdder-facing ports (S1) ----
    def o_OFFLOAD_S1(self, itf):
        self.itf_bind('offload_s1', itf,
            signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_OFFLOAD_GRANT_S1(self):
        return gvsoc.systree.SlaveItf(self, 'offload_grant_s1',
            signature='wire<IssOffloadInsnGrant<uint32_t>*>')

"""
XIF Decoder — Python Wrapper (Reference Copy)
================================================

GVSoC Python wrapper for the XifDecoder component.
Declares ports and points to the C++ source file for compilation.

Port summary:
  i_OFFLOAD_M         ← receives custom instructions from the core
  o_OFFLOAD_GRANT_M   → sends grant/result back to the core

  o_OFFLOAD_S1        → forwards iDMA instructions to iDMA controller
  i_OFFLOAD_GRANT_S1  ← receives grant from iDMA controller

  o_OFFLOAD_S2        → forwards RedMulE instructions to LightRedmule
  i_OFFLOAD_GRANT_S2  ← receives grant from LightRedmule

  Fractal ports       ↔ FractalSync barrier network

Original file: gvsoc/pulp/pulp/chips/magia/xif_decoder/xif_decoder.py
Author: Lorenzo Zuolo (Chips-IT)
"""

import gvsoc.systree

class XifDecoder(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.add_sources(['pulp/chips/magia/xif_decoder/xif_decoder.cpp'])

    # ---- Core-facing ports ----
    def i_OFFLOAD_M(self):
        return gvsoc.systree.SlaveItf(self, 'offload_m', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT_M(self, itf):
        self.itf_bind('offload_grant_m', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    # ---- iDMA-facing ports (S1) ----
    def o_OFFLOAD_S1(self, itf):
        self.itf_bind('offload_s1', itf, signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_OFFLOAD_GRANT_S1(self):
        return gvsoc.systree.SlaveItf(self, 'offload_grant_s1', signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    # ---- RedMulE-facing ports (S2) ----
    def o_OFFLOAD_S2(self, itf):
        self.itf_bind('offload_s2', itf, signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_OFFLOAD_GRANT_S2(self):
        return gvsoc.systree.SlaveItf(self, 'offload_grant_s2', signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    # ---- FractalSync ports ----
    def o_XIF_2_FRACTAL_EAST_WEST(self, itf):
        self.itf_bind('fractal_ew_input_port', itf, signature='wire<PortReq<uint32_t>*>')
    def i_FRACTAL_2_XIF_EAST_WEST(self):
        return gvsoc.systree.SlaveItf(self, 'fractal_ew_output_port', signature='wire<PortResp<uint32_t>*>')
    def o_XIF_2_FRACTAL_NORD_SUD(self, itf):
        self.itf_bind('fractal_ns_input_port', itf, signature='wire<PortReq<uint32_t>*>')
    def i_FRACTAL_2_XIF_NORD_SUD(self):
        return gvsoc.systree.SlaveItf(self, 'fractal_ns_output_port', signature='wire<PortResp<uint32_t>*>')
    def o_XIF_2_NEIGHBOUR_FRACTAL_EAST_WEST(self, itf):
        self.itf_bind('neighbour_fractal_ew_input_port', itf, signature='wire<PortReq<uint32_t>*>')
    def i_NEIGHBOUR_FRACTAL_2_XIF_EAST_WEST(self):
        return gvsoc.systree.SlaveItf(self, 'neighbour_fractal_ew_output_port', signature='wire<PortResp<uint32_t>*>')
    def o_XIF_2_NEIGHBOUR_FRACTAL_NORD_SUD(self, itf):
        self.itf_bind('neighbour_fractal_ns_input_port', itf, signature='wire<PortReq<uint32_t>*>')
    def i_NEIGHBOUR_FRACTAL_2_XIF_NORD_SUD(self):
        return gvsoc.systree.SlaveItf(self, 'neighbour_fractal_ns_output_port', signature='wire<PortResp<uint32_t>*>')

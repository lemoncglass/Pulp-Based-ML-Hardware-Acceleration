"""
LightRedMulE — Python Wrapper (Reference Copy)
=================================================

GVSoC Python wrapper for the LightRedmule accelerator component.
This file tells GVSoC which C++ source to compile, what configuration
parameters to pass, and what ports the component exposes.

Configuration parameters (set here, read in C++ constructor via get_js_config):
  tcdm_bank_width  : bytes per TCDM bank (e.g., 4)
  tcdm_bank_number : number of TCDM banks (e.g., 32)
  elem_size        : max bytes per element (2 for FP16, 1 for INT8)
  ce_height        : compute engine array height (rows of PEs)
  ce_width         : compute engine array width (cols of PEs)
  ce_pipe          : pipeline depth of the compute engine
  queue_depth      : max outstanding TCDM requests
  fold_tiles_mapping : tile mapping strategy (0 = default)
  loc_base         : local TCDM base address offset

WARNING: ce_height and ce_width are SWAPPED in add_properties() per the
original author's note — the original model was based on a transposed
RedMulE architecture.

Port summary:
  i_INPUT    ← IoSlave  : MMIO register access
  i_OFFLOAD  ← WireSlave: XIF instruction offload from XifDecoder
  o_OFFLOAD_GRANT → WireMaster: grant/result back to XifDecoder
  o_TCDM     → IoMaster : memory access to TCDM banks
  o_IRQ      → WireMaster: interrupt line to core

Original file: gvsoc/pulp/pulp/light_redmule/light_redmule.py
Authors: Chi Zhang (ETH Zurich), Lorenzo Zuolo (Chips-IT)
"""

import gvsoc.systree

class LightRedmule(gvsoc.systree.Component):

    def __init__(self, parent, name,
                 tcdm_bank_width, tcdm_bank_number, elem_size,
                 ce_height, ce_width, ce_pipe,
                 queue_depth=128, fold_tiles_mapping=0, loc_base=0):

        super().__init__(parent, name)

        self.add_sources(['pulp/light_redmule/light_redmule.cpp'])

        self.add_properties({
            'tcdm_bank_width'   : tcdm_bank_width,
            'tcdm_bank_number'  : tcdm_bank_number,
            'elem_size'         : elem_size,
            # WARNING: height/width swapped per original author's note
            'ce_height'         : ce_width,
            'ce_width'          : ce_height,
            'ce_pipe'           : ce_pipe,
            'queue_depth'       : queue_depth,
            'fold_tiles_mapping': fold_tiles_mapping,
            'loc_base'          : loc_base,
        })

    def i_INPUT(self):
        """MMIO slave port for register access."""
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_OFFLOAD(self):
        """XIF offload slave — receives custom instructions from core via XifDecoder."""
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf):
        """XIF grant master — sends writeback/unstall to core via XifDecoder."""
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_TCDM(self, itf):
        """TCDM memory master port — reads/writes matrix data."""
        self.itf_bind('tcdm', itf, signature='io')

    def o_IRQ(self, itf):
        """IRQ line — active-high pulse when computation finishes."""
        self.itf_bind('done_irq', itf, signature='wire<bool>')

"""
HWPE Interleaver — Python Wrapper (Reference Copy)
=====================================================

GVSoC Python wrapper for the HWPEInterleaver component.
Splits RedMulE's flat TCDM requests across memory banks.

Original file: gvsoc/pulp/pulp/light_redmule/hwpe_interleaver.py
Author: Germain Haugou (GreenWaves Technologies)
"""

import gvsoc.systree

class HWPEInterleaver(gvsoc.systree.Component):

    def __init__(self, parent, slave, nb_master_ports, nb_banks, bank_width):
        super(HWPEInterleaver, self).__init__(parent, slave)

        self.add_sources(['pulp/light_redmule/hwpe_interleaver.cpp'])

        self.add_properties({
            'nb_banks': nb_banks,
            'bank_width': bank_width
        })

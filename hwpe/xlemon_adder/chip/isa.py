#
# XLemon Adder — ISA Definition
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 University of Missouri - Kansas City
#
# Defines the xladd custom RISC-V instruction so the ISS decoder
# knows how to parse it.
#
# xladd encoding (custom-1 opcode):
#
#   31   27 26 25 24  20 19   15 14  12 11   7 6      0
#   ┌───────┬────┬──────┬───────┬──────┬──────┬────────┐
#   │  rs3  │ 00 │ rs2  │  rs1  │ 000  │00000 │0101011 │
#   └───────┴────┴──────┴───────┴──────┴──────┴────────┘
#
# The ISS matches the opcode, extracts rs1/rs2/rs3 register values,
# and calls xladd_exec() which fires the offload wire.
#

from cpu.iss.isa_gen.isa_gen import *
from cpu.iss.isa_gen.isa_riscv_gen import *

# Register operand layout: 3 input registers (rs1, rs2, rs3)
# Same layout as Magia's Format_DMADATA / marith
Format_XLADD = [
    InReg(0, Range(15, 5)),   # rs1 = pointer to operand A
    InReg(1, Range(20, 5)),   # rs2 = pointer to operand B
    InReg(2, Range(27, 5)),   # rs3 = pointer to result
]

class Rv32xlemon(IsaSubset):
    """ISA subset for the xladd custom instruction.

    The bit pattern uses dashes for 'don't care' bits (rs3, rs2, rs1)
    and fixes the remaining fields:
      bits [26:25] = 00
      bits [14:12] = 000  (funct3)
      bits [11:7]  = 00000 (rd, unused)
      bits [6:0]   = 0101011 (custom-1 opcode)
    """
    def __init__(self):
        super().__init__(name='xlemon', instrs=[
            Instr('xladd', Format_XLADD, '-----00 ----- ----- 000 00000 0101011'),
        ])

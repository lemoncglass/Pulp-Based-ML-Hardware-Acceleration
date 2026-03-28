"""
ISA Definitions for Magia Custom Instructions (Reference Copy)
================================================================

This file defines the RISC-V custom instruction subsets used by the
Magia chip's coprocessors.  GVSoC's ISA generator reads these definitions
to teach the ISS (Instruction Set Simulator) how to decode and dispatch
unknown opcodes.

Three instruction subsets are defined:

1. Rv32redmule — RedMulE matrix multiply instructions
   ┌──────────┬──────────────────────────────────────────────┐
   │ mcnfig   │ 0000000 rs2   rs1   000 rd    0001011       │
   │          │ Sets M,N,K dimensions.                       │
   │          │ rs1 = {K_SIZE[31:16], M_SIZE[15:0]}          │
   │          │ rs2 = N_SIZE                                 │
   ├──────────┼──────────────────────────────────────────────┤
   │ marith   │ rs3 00 rs2   rs1   fmt imm   0101011        │
   │          │ Sets addresses + format, triggers compute.   │
   │          │ rs1 = X_addr, rs2 = W_addr, rs3 = Y_addr    │
   │          │ imm[7:0] = {op_sel[5:3], format[2:0]}       │
   └──────────┴──────────────────────────────────────────────┘

2. iDMA_Ctrl — DMA engine control instructions
   ┌──────────┬──────────────────────────────────────────────┐
   │ dmcnf    │ Configure DMA                    (1011011)   │
   │ dm1d2d3d │ 1D/2D/3D transfer               (1111011)   │
   │ dmstr    │ Stride configuration             (1111011)   │
   └──────────┴──────────────────────────────────────────────┘

3. FSync — FractalSync barrier instruction
   ┌──────────┬──────────────────────────────────────────────┐
   │ fsync    │ 0000000 rs2   rs1   010 rd    1011011       │
   │          │ Barrier synchronization across tiles.        │
   │          │ rs1 = aggregation, rs2 = direction           │
   └──────────┴──────────────────────────────────────────────┘

Original file: gvsoc/pulp/pulp/chips/magia/cv32/isa.py
Author: Lorenzo Zuolo (Chips-IT)
"""

from cpu.iss.isa_gen.isa_gen import *
from cpu.iss.isa_gen.isa_riscv_gen import *

# ---- DMA data format: 3 input registers ----
Format_DMADATA = [
    InReg (0, Range(15, 5)),
    InReg (1, Range(20, 5)),
    InReg (2, Range(27, 5)),
]

class iDMA_Ctrl(IsaSubset):
    def __init__(self):
        super().__init__(name='iDMA_Ctrl', instrs=[
            Instr('dmcnf',        Format_Z,         '000011- 00000 00000 000 00000 1011011'),
            Instr('dm1d2d3d',     Format_DMADATA,   '-----0- ----- ----- 0-- 00000 1111011'),
            Instr('dmstr',        Format_Z,         '000000- 00000 00000 111 00000 1111011'),
        ])

# ---- RedMulE marith format: 3 input regs + unsigned immediate ----
Format_MARITH = [
    InReg (0, Range(15, 5)),       # rs1 = X_addr
    InReg (1, Range(20, 5)),       # rs2 = W_addr
    InReg (2, Range(27, 5)),       # rs3 = Y_addr
    UnsignedImm(0, Range(7, 8)),   # imm = {op_sel, format}
]

class Rv32redmule(IsaSubset):
    """RedMulE instruction subset.

    mcnfig: opcode = 0b0001011 (custom-0)
      - Uses Format_R (standard R-type with rs1, rs2, rd)
      - The ISS handler (mcnfig_exec) packs rs1→arg_a, rs2→arg_b

    marith: opcode = 0b0101011 (custom-1)
      - Uses Format_MARITH (3 regs + unsigned immediate)
      - The ISS handler (marith_exec) packs rs1→arg_a, rs2→arg_b,
        rs3→arg_c, imm→arg_d
    """
    def __init__(self):
        super().__init__(name='redmule', instrs=[
            Instr('mcnfig',    Format_R     , '0000000 ----- ----- 000 00000 0001011'),
            Instr('marith',    Format_MARITH, '-----00 ----- ----- --- ----- 0101011'),
        ])

class FSync(IsaSubset):
    """FractalSync barrier instruction.

    fsync: opcode = 0b1011011, funct3 = 010
      - rs1 = aggregation level
      - rs2 = barrier direction / ID
    """
    def __init__(self):
        super().__init__(name='fractal_sync', instrs=[
            Instr('fsync',    Format_R     , '0000000 ----- ----- 010 00000 1011011'),
        ])

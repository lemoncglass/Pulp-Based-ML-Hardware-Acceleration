/*
 * Toy Adder HWPE — Architecture / Register Map
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Cody Glassbrenner
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 * 
 * This defines the memory-mapped register layout that software uses to
 * talk to the accelerator.  Every HWPE on PULP follows the same
 * convention for the first six "control" registers (trigger, acquire,
 * finished, status, running_job, soft_clear).  After those come the
 * job-specific registers — for the toy adder that is just three:
 *   operand A, operand B, and the result.
 * 
 */

#ifndef __ARCHI_TOY_ADDER_H__
#define __ARCHI_TOY_ADDER_H__

/*
 * |=======================================================================|
 * || Control registers (standard HWPE interface)                         ||
 * |=======================================================================|
 * || # reg |  offset  |  content                                         ||
 * ||-------+----------+--------------------------------------------------||
 * ||    0  |  0x0000  |  TRIGGER      – write 0 to start a job           ||
 * ||    1  |  0x0004  |  ACQUIRE      – read to check if HWPE is free    ||
 * ||    2  |  0x0008  |  FINISHED     – event-enable mask                ||
 * ||    3  |  0x000C  |  STATUS       – 0 = idle, 1 = busy               ||
 * ||    4  |  0x0010  |  RUNNING_JOB  – id of running job                ||
 * ||    5  |  0x0014  |  SOFT_CLEAR   – write to reset state             ||
 * |=======================================================================|
 * || Job-dependent registers                                             ||
 * |=======================================================================|
 * ||    0  |  0x0040  |  A_PTR – pointer / value of operand A            ||
 * ||    1  |  0x0044  |  B_PTR – pointer / value of operand B            ||
 * ||    2  |  0x0048  |  RES_PTR – pointer to store the result           ||
 * |=======================================================================|
 */

/* ---- Base address (same region as other PULP cluster accelerators) ---- */
#define TOY_ADDER_BASE_ADD   0x10201000

/* ---- Standard HWPE control register offsets ---- */
#define TOY_ADDER_TRIGGER       0x00
#define TOY_ADDER_ACQUIRE       0x04
#define TOY_ADDER_FINISHED      0x08
#define TOY_ADDER_STATUS        0x0C
#define TOY_ADDER_RUNNING_JOB   0x10
#define TOY_ADDER_SOFT_CLEAR    0x14

/* ---- Job-specific register offsets ---- */
#define TOY_ADDER_REG_OFFS     0x40
#define TOY_ADDER_REG_A_PTR    0x00   // relative to REG_OFFS   -> REG_OFFS + 0x00
#define TOY_ADDER_REG_B_PTR    0x04   // ^                      -> REG_OFFS + 0x04
#define TOY_ADDER_REG_RES_PTR  0x08   // ^                      -> REG_OFFS + 0x08

/* ---- Event used by the HWPE to signal completion ---- */
#define TOY_ADDER_EVT0         12

#endif /* __ARCHI_TOY_ADDER_H__ */

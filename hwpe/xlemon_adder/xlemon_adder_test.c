/*
 * XLemon Adder HWPE — Test program (XIF version)
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 University of Missouri - Kansas City
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * === XIF vs MMIO test comparison ===
 *
 * The MMIO lemon_adder_test.c did:
 *   1. lemon_adder_set_a(&operand_a);       // store to MMIO register
 *   2. lemon_adder_set_b(&operand_b);       // store to MMIO register
 *   3. lemon_adder_set_res_ptr(&result);    // store to MMIO register
 *   4. hwpe_trigger_job();                   // store to TRIGGER register
 *   5. eu_evt_maskWaitAndClr(1 << EVT0);    // wait for IRQ
 *
 * This XIF version does:
 *   1. xlemon_adder_xladd(&a, &b, &res);   // single custom instruction!
 *   2. eu_evt_maskWaitAndClr(1 << EVT0);    // wait for IRQ
 *
 * The custom instruction carries all three pointers in its register
 * operands (rs1, rs2, rs3), so configuration and triggering happen
 * in ONE instruction instead of four separate memory stores.
 *
 * === Important: This test is a reference implementation ===
 *
 * The xladd custom instruction requires a CV32E40X core with the
 * eXtension Interface (XIF) and a XifDecoder wired to route opcode
 * 0b0101011 to the XLemonAdder.  This infrastructure exists in
 * Magia-style chips but NOT in pulp-open.
 *
 * To actually run this test, you would need to:
 *   1. Use a Magia tile (or similar) with CV32E40X + XifDecoder
 *   2. Register the xlemon_adder ISA subset in the core's ISA config
 *   3. Add a routing case in the XifDecoder for opcode 0b0101011
 *   4. Wire the XifDecoder → XLemonAdder ports in the tile builder
 *
 * For now, this serves as a complete reference showing how software
 * talks to an XIF-based accelerator.
 */

#include "pmsis.h"
#include <stdint.h>
#include <stdio.h>

/* XIF HAL — provides xlemon_adder_xladd() using inline assembly */
#include "hal_xlemon_adder.h"

/* Stimulus data — same as the MMIO version */
#include "inc/a_input.h"
#include "inc/b_input.h"
#include "inc/golden.h"

/* -------------------------------------------------------------------
 * Allocate operands and result in L1 cluster TCDM.
 * The accelerator accesses these through its TCDM master port,
 * exactly the same as in the MMIO version — the memory side
 * doesn't change between MMIO and XIF.
 * ------------------------------------------------------------------- */
PI_L1 int32_t operand_a = OPERAND_A;
PI_L1 int32_t operand_b = OPERAND_B;
PI_L1 int32_t result    = 0;

/* -------------------------------------------------------------------
 * cluster_entry — runs on cluster core 0
 *
 * Compare with the MMIO version:
 *   MMIO: 4 HAL calls (set_a, set_b, set_res_ptr, trigger_job)
 *   XIF:  1 HAL call  (xlemon_adder_xladd)
 * ------------------------------------------------------------------- */
void cluster_entry(void *arg)
{
    printf("\n--------------------------------------\n");
    printf("[xlemon_adder] Operand A (L1) = %d\n", operand_a);
    printf("[xlemon_adder] Operand B (L1) = %d\n", operand_b);

    /* ---------- Fire the xladd custom instruction --------------- */
    /* This single call replaces ALL the MMIO register writes AND
     * the trigger.  The inline assembly:
     *   1. Moves &operand_a into t0 (rs1)
     *   2. Moves &operand_b into t1 (rs2)
     *   3. Moves &result    into t2 (rs3)
     *   4. Emits .word for the xladd instruction
     *
     * The core fires the instruction, the ISS packages t0/t1/t2
     * into an IssOffloadInsn and sends it over the XIF offload wire
     * to the XifDecoder, which routes it to XLemonAdder. */
    xlemon_adder_xladd((unsigned int)(intptr_t)&operand_a,
                       (unsigned int)(intptr_t)&operand_b,
                       (unsigned int)(intptr_t)&result);

    /* ---------- Wait for the accelerator to finish -------------- */
    /* The IRQ mechanism is the same as MMIO — the accelerator
     * asserts its IRQ line when done, and the event unit wakes
     * the core. */
    eu_evt_maskWaitAndClr(1 << XLEMON_ADDER_EVT0);

    /* ---------- Verify against golden output --------------------- */
    printf("[xlemon_adder] Result         = %d\n", result);
    printf("[xlemon_adder] Golden         = %d\n", GOLDEN_SUM);

    if (result == GOLDEN_SUM) {
        printf("\n<<<<<<<<<<>>>>>>>>>>\n[xlemon_adder] PASS\n\n<<<<<<<<<<>>>>>>>>>>\n");
    } else {
        printf("\n/!\\ ---------- /!\\\n[xlemon_adder] FAIL: \nexpected %d, got %d\n\n/!\\ ---------- /!\\\n", GOLDEN_SUM, result);
    }
}

/* -------------------------------------------------------------------
 * test_entry — called from the Fabric Controller (FC)
 * ------------------------------------------------------------------- */
static int test_entry(void)
{
    struct pi_device cluster_dev;
    struct pi_cluster_conf cl_conf;
    struct pi_cluster_task cl_task;

    pi_cluster_conf_init(&cl_conf);
    pi_open_from_conf(&cluster_dev, &cl_conf);

    if (pi_cluster_open(&cluster_dev))
    {
        printf("[xlemon_adder] ERROR: could not open cluster\n");
        return -1;
    }

    pi_cluster_send_task_to_cl(
        &cluster_dev,
        pi_cluster_task(&cl_task, cluster_entry, NULL)
    );

    pi_cluster_close(&cluster_dev);
    return 0;
}

static void test_kickoff(void *arg)
{
    int ret = test_entry();
    pmsis_exit(ret);
}

int main(void)
{
    return pmsis_kickoff((void *)test_kickoff);
}


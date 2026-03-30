/*
 * Lemon Adder HWPE — Test program
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Cody Glassbrenner
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * This follows the same structure as a real HWPE test (e.g. redmule):
 *   1. Input data is defined in header files under inc/.
 *   2. Data is allocated in L1 cluster memory (PI_L1).
 *   3. The HAL is used to program the accelerator registers.
 *   4. The accelerator is triggered and the core waits for the IRQ.
 *   5. The result is checked against a golden reference.
 */

#include "pmsis.h"
#include <stdint.h>
#include <stdio.h>

/* HAL & register map (includes archi_lemon_adder.h automatically) */
#include "hal_lemon_adder.h"

/* Stimulus data — one file per operand, like redmule's x_input / w_input */
#include "inc/a_input.h"
#include "inc/b_input.h"
#include "inc/golden.h"

/* -------------------------------------------------------------------
 * Allocate operands and result in L1 cluster TCDM.
 * PI_L1 tells the linker to place these in the cluster's scratchpad so
 * an accelerator on the cluster interconnect can reach them directly.
 * ------------------------------------------------------------------- */
PI_L1 int32_t operand_a = OPERAND_A;
PI_L1 int32_t operand_b = OPERAND_B;
PI_L1 int32_t result    = 0;

/* -------------------------------------------------------------------
 * cluster_entry — runs on cluster core 0
 * ------------------------------------------------------------------- */
void cluster_entry(void *arg)
{
    printf("\n--------------------------------------\n");
    printf("[lemon_adder] Operand A (L1) = %d\n", operand_a);
    printf("[lemon_adder] Operand B (L1) = %d\n", operand_b);

    /* ---------- Program the HWPE registers via HAL -------------- */
    // Pass the L1 addresses of our operands and result buffer
    lemon_adder_set_a((unsigned int)(intptr_t)&operand_a);
    lemon_adder_set_b((unsigned int)(intptr_t)&operand_b);
    lemon_adder_set_res_ptr((unsigned int)(intptr_t)&result);

    /* ---------- Launch the accelerator and wait for it ---------- */
    // TRIGGER tells the HWPE to start; the core then sleeps until
    // the HWPE fires its IRQ on event line LEMON_ADDER_EVT0 (acc_0).
    hwpe_trigger_job();
    eu_evt_maskWaitAndClr(1 << LEMON_ADDER_EVT0);

    /* ---------- Verify against golden output --------------------- */
    printf("[lemon_adder] Result         = %d\n", result);
    printf("[lemon_adder] Golden         = %d\n", GOLDEN_SUM);

    if (result == GOLDEN_SUM) {
        printf("\n<<<<<<<<<<>>>>>>>>>>\n[lemon_adder] PASS\n\n<<<<<<<<<<>>>>>>>>>>\n");
    } else {
        printf("\n/!\\ ---------- /!\\\n[lemon_adder] FAIL: \nexpected %d, got %d\n\n/!\\ ---------- /!\\\n", GOLDEN_SUM, result);
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
        printf("[lemon_adder] ERROR: could not open cluster\n");
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


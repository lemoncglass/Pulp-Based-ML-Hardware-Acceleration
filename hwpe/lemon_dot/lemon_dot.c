/*
 * Lemon Dot HWPE — Test program (RISC-V application)
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Cody Glassbrenner
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com> (heavily assisted by Claude Opus 4.6 agent)
 *
 * This program runs on the RISC-V core inside the PULP cluster.
 * It demonstrates the full HWPE programming flow:
 *
 *   1. Allocate input matrices A, B and output C in L1 TCDM
 *   2. Program the HWPE's job registers via the HAL (→ MMIO stores)
 *   3. TRIGGER the accelerator (→ one more MMIO store)
 *   4. Sleep until the HWPE fires its IRQ (→ p.elw instruction)
 *   5. Compare the result against the golden reference
 *
 * Run `make disasm` after building to see the RISC-V assembly for
 * each of these steps.  Search for "10201" in the .dump file to find
 * every HWPE register access.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │ ISA-level breakdown of what happens:                            │
 * │                                                                 │
 * │  Step 2 (register writes):                                      │
 * │    lui  a5, 0x10201       ← load HWPE base into register        │
 * │    sw   a0, 0x40(a5)      ← write A_PTR                         │
 * │    sw   a1, 0x44(a5)      ← write B_PTR                         │
 * │    ...                                                          │
 * │                                                                 │
 * │  Step 3 (trigger):                                              │
 * │    sw   zero, 0x00(a5)    ← write 0 to TRIGGER register         │
 * │                                                                 │
 * │  Step 4 (wait for IRQ):                                         │
 * │    p.elw  a0, 0(a5)       ← PULP ISA extension!                 │
 * │    This is "event load word" — the core goes to sleep and       │
 * │    wakes only when the event unit receives the HWPE's IRQ.      │
 * │    It's the hardware equivalent of WFI + clear-on-wake.         │
 * └─────────────────────────────────────────────────────────────────┘
 */

#include "pmsis.h"
#include <stdint.h>
#include <stdio.h>

/* HAL & register map */
#include "hal_lemon_dot.h"

/* Test data — matrices and golden expected output */
#include "inc/matrix_a.h"
#include "inc/matrix_b.h"
#include "inc/golden.h"

/* -------------------------------------------------------------------
 * Allocate matrices in L1 cluster TCDM.
 *
 * PI_L1 tells the linker to place these in the cluster's scratchpad
 * memory (TCDM — Tightly Coupled Data Memory).  The HWPE can access
 * TCDM directly through the cluster interconnect — no cache, no
 * coherency issues, just a flat address space shared between the
 * cores and the accelerator.
 * ------------------------------------------------------------------- */
PI_L1 int32_t matrix_a[DOT_M * DOT_K] = MATRIX_A_DATA;
PI_L1 int32_t matrix_b[DOT_K * DOT_N] = MATRIX_B_DATA;
PI_L1 int32_t matrix_c[DOT_M * DOT_N] = {0};  /* output — will be filled by HWPE */
PI_L1 int32_t golden[DOT_M * DOT_N]   = GOLDEN_DATA;

/* -------------------------------------------------------------------
 * print_matrix — helper to dump a matrix to the console
 * ------------------------------------------------------------------- */
static void print_matrix(const char *name, const int32_t *mat,
                         int rows, int cols)
{
    printf("  %s (%d x %d):\n", name, rows, cols);
    for (int i = 0; i < rows; i++) {
        printf("    [");
        for (int j = 0; j < cols; j++) {
            printf(" %4d", mat[i * cols + j]);
        }
        printf(" ]\n");
    }
}

/* -------------------------------------------------------------------
 * cluster_entry — runs on cluster core 0
 * ------------------------------------------------------------------- */
void cluster_entry(void *arg)
{
    printf("\n============================================\n");
    printf("[lemon_dot] Matrix multiply C = A x B\n");
    printf("[lemon_dot] Dimensions: (%d x %d) * (%d x %d) -> (%d x %d)\n",
           DOT_M, DOT_K, DOT_K, DOT_N, DOT_M, DOT_N);
    printf("============================================\n\n");

    /* -- Show inputs -- */
    print_matrix("A", matrix_a, DOT_M, DOT_K);
    print_matrix("B", matrix_b, DOT_K, DOT_N);

    /* ------------------------------------------------------------------
     * Program the HWPE registers via HAL.
     *
     * Each of these calls compiles to a `sw` instruction writing to
     * the HWPE's MMIO address space.  The HWPE latches the values
     * into its internal register file and uses them when triggered.
     * ------------------------------------------------------------------ */
    lemon_dot_set_a_ptr((unsigned int)(uintptr_t)matrix_a);
    lemon_dot_set_b_ptr((unsigned int)(uintptr_t)matrix_b);
    lemon_dot_set_c_ptr((unsigned int)(uintptr_t)matrix_c);
    lemon_dot_set_m(DOT_M);
    lemon_dot_set_k(DOT_K);
    lemon_dot_set_n(DOT_N);

    printf("[lemon_dot] HWPE registers programmed\n");
    printf("[lemon_dot]   A_PTR = 0x%08x\n", (unsigned int)(uintptr_t)matrix_a);
    printf("[lemon_dot]   B_PTR = 0x%08x\n", (unsigned int)(uintptr_t)matrix_b);
    printf("[lemon_dot]   C_PTR = 0x%08x\n", (unsigned int)(uintptr_t)matrix_c);
    printf("[lemon_dot]   M=%d  K=%d  N=%d\n\n", DOT_M, DOT_K, DOT_N);

    /* ------------------------------------------------------------------
     * Trigger the HWPE and wait for completion.
     *
     * hwpe_trigger_job() writes 0 to the TRIGGER register → the HWPE
     * transitions from IDLE to its LOAD_A state.
     *
     * eu_evt_maskWaitAndClr() compiles to a `p.elw` instruction.
     * The core goes to sleep (clock-gated) until the event unit
     * receives IRQ #12 (LEMON_DOT_EVT0), which the HWPE asserts
     * after writing the last element of C.
     * ------------------------------------------------------------------ */
    printf("[lemon_dot] Triggering HWPE...\n");
    hwpe_trigger_job();
    eu_evt_maskWaitAndClr(1 << LEMON_DOT_EVT0);
    printf("[lemon_dot] HWPE finished!\n\n");

    /* -- Show result -- */
    print_matrix("C (computed)", matrix_c, DOT_M, DOT_N);
    print_matrix("C (golden)",   golden,   DOT_M, DOT_N);

    /* ------------------------------------------------------------------
     * Verify each element against the golden reference.
     * ------------------------------------------------------------------ */
    int errors = 0;
    for (int i = 0; i < DOT_M * DOT_N; i++) {
        if (matrix_c[i] != golden[i]) {
            printf("[lemon_dot] MISMATCH at [%d][%d]: got %d, expected %d\n",
                   i / DOT_N, i % DOT_N, matrix_c[i], golden[i]);
            errors++;
        }
    }

    printf("\n");
    if (errors == 0) {
        printf("<<<<<<<<<<>>>>>>>>>>\n");
        printf("[lemon_dot] PASS — all %d elements match!\n", DOT_M * DOT_N);
        printf("\n<<<<<<<<<<>>>>>>>>>>\n");
    } else {
        printf("/!\\ ---------- /!\\\n");
        printf("[lemon_dot] FAIL — %d / %d mismatches\n", errors, DOT_M * DOT_N);
        printf("\n/!\\ ---------- /!\\\n");
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

    if (pi_cluster_open(&cluster_dev)) {
        printf("[lemon_dot] ERROR: could not open cluster\n");
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

/*
 * LightRedMulE — Class Definition (Header)
 * ==========================================
 *
 * This is the heart of the model: the LightRedmule class that inherits from
 * vp::Component.  It declares:
 *
 *   1. Static callback methods (req, offload_sync, fsm_handler) that GVSoC
 *      invokes when events arrive on the component's ports.
 *
 *   2. Internal helper methods for tiling math, address generation, the
 *      iteration FSM, and compute dispatch.
 *
 *   3. All member variables: GVSoC ports/interfaces, FSM state, configuration
 *      registers, tiling metadata, and local compute buffers.
 *
 * The class supports TWO modes of operation:
 *
 *   • MMIO mode  — The core writes config registers via store instructions,
 *                  then triggers compute with a load to offset 32 (sync) or
 *                  36 (async).  This is the original RedMulE protocol.
 *
 *   • XIF mode   — The core issues mcnfig / marith custom instructions.
 *                  The CV32E40X core sends them over the eXtension Interface
 *                  (XIF) to the XifDecoder, which routes them here via the
 *                  offload_sync callback.  This is the Magia chip protocol.
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 * Authors: Chi Zhang (ETH Zurich), Lorenzo Zuolo (Chips-IT), Alex Marchioni (Chips-IT)
 */

#pragma once

/* ---- GVSoC framework ---- */
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>

/* ---- Standard library ---- */
#include <cstdint>
#include <cstring>
#include <queue>

/* ---- XIF offload types (IssOffloadInsn, IssOffloadInsnGrant) ---- */
#include <cpu/iss/include/offload.hpp>

/* ---- Our shared types & enums ---- */
#include "light_redmule_types.h"
#include "matmul_kernels.h"


class LightRedmule : public vp::Component
{
public:
    LightRedmule(vp::ComponentConf &config);

    /* ================================================================
     *  Static GVSoC callbacks  (must be static — GVSoC passes `this`
     *  via the vp::Block* parameter)
     * ================================================================ */

    /** MMIO register read/write handler.
     *  Called when the core issues a load/store to the HWPE address range.
     *  Offsets 0–28: config register writes (M, N, K, X, W, Y, Z addrs).
     *  Offset 32 read: synchronous trigger (core stalls until done).
     *  Offset 36 read: asynchronous trigger (core continues, polls/waits IRQ).
     *  Offset 40 read: async wait (stall until ongoing compute finishes).
     */
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    /** XIF offload handler.
     *  Called when the XifDecoder forwards a custom instruction to us.
     *  Decodes opcode[6:0]:
     *    0b0001011 (mcnfig) → extract M, N, K from arg_a/arg_b
     *    0b0101011 (marith) → extract X/W/Y ptrs + format, trigger FSM
     */
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);

    /** Clock-driven FSM handler.
     *  Runs every cycle while the accelerator is active, advancing through:
     *    PRELOAD → ROUTINE → STORING → FINISHED → ACKNOWLEDGE/IDLE
     */
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    /* ================================================================
     *  Internal helpers
     * ================================================================ */

    /** Parse the op_format field from marith to determine compute_able code */
    uint32_t op_foramt_parser(uint32_t op_format);

    /** Send a TCDM memory request through the tcdm master port */
    vp::IoReqStatus send_tcdm_req();

    /** Initialize tiling metadata from the current M, N, K configuration */
    void init_redmule_meta_data();

    /* --- Address generation --- */
    uint32_t tmp_next_addr();
    uint32_t next_addr();
    uint32_t calculate_tile_base_address(uint32_t base, uint32_t stride,
                                          uint32_t tile_col, uint32_t tile_row,
                                          uint32_t i, uint32_t j);
    uint32_t inc_addr(uint32_t addr, uint32_t stride, uint32_t tile_row);

    /* --- Iteration control --- */
    uint32_t next_iteration();
    uint32_t get_redmule_array_runtime();
    uint32_t get_routine_access_block_number();
    uint32_t get_preload_access_block_number();
    uint32_t get_storing_access_block_number();
    uint32_t get_routine_to_storing_latency();

    /* --- Compute dispatch --- */
    void process_iter_instruction();
    void process_compute();

    /* ================================================================
     *  GVSoC interfaces & ports
     * ================================================================ */

    vp::Trace           trace;          /**< Debug trace channel */

    vp::IoSlave         input_itf;      /**< MMIO slave port (config registers) */

    /** XIF offload slave — receives custom instructions from the core */
    vp::WireSlave<IssOffloadInsn<uint32_t> *>      offload_itf;
    /** XIF grant master — sends write-back / stall release to the core */
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf;

    vp::WireMaster<bool> done;          /**< IRQ line to the core */
    vp::IoMaster         tcdm_itf;      /**< TCDM memory master port */

    /* ================================================================
     *  FSM state
     * ================================================================ */

    vp::IoReq *         redmule_query;  /**< Stalled MMIO request (sync trigger) */
    vp::IoReq *         tcdm_req;       /**< Reusable TCDM request object */
    vp::ClockEvent *    fsm_event;      /**< Clock event driving the FSM */
    vp::reg_32          state;          /**< Current FSM state (redmule_state) */

    uint32_t            tcdm_block_total;   /**< Total TCDM blocks to transfer in current phase */
    uint32_t            fsm_counter;        /**< Blocks transferred so far in current phase */
    uint32_t            fsm_timestamp;      /**< Cycle count within current phase */
    std::queue<uint32_t> pending_req_queue; /**< Outstanding request completion timestamps */

    int64_t             timer_start;    /**< Absolute time at FSM start (for reporting) */
    int64_t             cycle_start;    /**< Cycle count at FSM start */
    int64_t             total_runtime;  /**< Accumulated runtime across all GEMMs */
    int64_t             num_matmul;     /**< Number of GEMMs completed */

    /* ================================================================
     *  Hardware configuration (from Python wrapper / JSON config)
     * ================================================================ */

    uint32_t            tcdm_bank_width;    /**< Bytes per TCDM bank (e.g., 4) */
    uint32_t            tcdm_bank_number;   /**< Number of TCDM banks (e.g., 32) */
    uint32_t            elem_size;          /**< Max bytes per element (2 for FP16) */
    uint32_t            ce_height;          /**< Compute-engine array height */
    uint32_t            ce_width;           /**< Compute-engine array width */
    uint32_t            ce_pipe;            /**< Pipeline depth of CE */
    uint32_t            queue_depth;        /**< Max outstanding TCDM requests */
    uint32_t            bandwidth;          /**< = bank_width × bank_number */
    uint32_t            fold_tiles_mapping; /**< Tile mapping strategy */
    uint64_t            loc_base;           /**< Local TCDM base address (subtracted from global addr) */
    uint32_t            compute_able;       /**< Which matmul kernel to use (0=none) */
    uint32_t            LOCAL_BUFFER_H;     /**< = ce_height */
    uint32_t            LOCAL_BUFFER_N;     /**< = bandwidth / elem_size */
    uint32_t            LOCAL_BUFFER_W;     /**< = ce_width × (ce_pipe + 1) */

    /* ================================================================
     *  Software-visible registers (set via MMIO writes or mcnfig/marith)
     * ================================================================ */

    uint32_t            m_size;     /**< Number of rows in X (and Z) */
    uint32_t            n_size;     /**< Inner dimension (cols of X = rows of W) */
    uint32_t            k_size;     /**< Number of cols in W (and Z) */
    uint32_t            x_addr;     /**< TCDM address of input matrix X */
    uint32_t            w_addr;     /**< TCDM address of weight matrix W */
    uint32_t            y_addr;     /**< TCDM address of bias matrix Y */
    uint32_t            z_addr;     /**< TCDM address of output matrix Z (often == y_addr) */

    /* ================================================================
     *  Tiling metadata (computed in init_redmule_meta_data)
     * ================================================================ */

    uint32_t            x_row_tiles, x_row_lefts;
    uint32_t            x_col_tiles, x_col_lefts;
    uint32_t            w_row_tiles, w_row_lefts;
    uint32_t            w_col_tiles, w_col_lefts;
    uint32_t            z_row_tiles, z_row_lefts;
    uint32_t            z_col_tiles, z_col_lefts;

    uint32_t            iter_i, iter_j, iter_k;     /**< Current tile iteration indices */
    uint32_t            iter_x_addr, iter_w_addr;   /**< Current tile base addresses */
    uint32_t            iter_y_addr, iter_z_addr;
    uint32_t            x_acc_block, w_acc_block;   /**< Blocks to access per buffer in current iteration */
    uint32_t            y_acc_block, z_acc_block;
    uint32_t            z_store_width, z_store_height;
    double              ideal_runtime;              /**< Ideal computation cycles (for utilization metric) */
    uint32_t            iter_instruction;           /**< Current micro-instruction (iter_instruction enum) */
    uint32_t            iter_x_row_ptr, iter_w_row_ptr;
    uint32_t            iter_y_row_ptr, iter_z_row_ptr;

    /* ================================================================
     *  Local compute buffers (model the on-chip SRAM inside the CE)
     * ================================================================ */

    uint8_t *           access_buffer;      /**< Scratch buffer for TCDM transfers */
    uint8_t *           y_buffer_preload;   /**< Preloaded Y (bias) tile */
    uint8_t *           w_buffer;           /**< Weight tile */
    uint8_t *           x_buffer;           /**< Input activation tile */
    uint8_t *           z_buffer_compute;   /**< Output accumulation tile */
    uint8_t *           z_buffer_previos;   /**< Previous Z tile (for overlapped store) */
};

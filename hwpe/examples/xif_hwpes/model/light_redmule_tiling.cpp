/*
 * LightRedMulE — Tiling Math & Address Generation
 * ==================================================
 *
 * This file implements the tiling logic that maps a large M×K = M×N · N×K
 * matrix multiply onto the fixed-size compute engine (ce_height × ce_width).
 *
 * Key concepts:
 *   • The matrices are divided into tiles that fit the CE.
 *   • Iteration order: for each (i, j, k) tile triple, load the
 *     corresponding sub-matrices of X, W, Y/Z and accumulate.
 *   • The address helpers compute TCDM byte addresses for each tile,
 *     accounting for the row-major layout and element size.
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#include "light_redmule.h"
#include <cstring>

/* ------------------------------------------------------------------ */
/*  init_redmule_meta_data()                                          */
/*  Called once at the start of each GEMM to compute tiling counts,   */
/*  starting addresses, and the ideal runtime estimate.               */
/* ------------------------------------------------------------------ */

void LightRedmule::init_redmule_meta_data()
{
    uint32_t buffer_h = this->ce_height;
    uint32_t buffer_w = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n = this->bandwidth / this->elem_size;

    /* X matrix (M × N): tiles along rows (height) and cols (width) */
    this->x_col_tiles = (this->m_size + buffer_h - 1) / buffer_h;
    this->x_col_lefts = this->m_size % buffer_h;
    this->x_row_tiles = (this->n_size + buffer_n - 1) / buffer_n;
    this->x_row_lefts = this->n_size % buffer_n;

    /* W matrix (N × K): tiles along rows and cols */
    this->w_col_tiles = this->x_row_tiles;
    this->w_col_lefts = this->x_row_lefts;
    this->w_row_tiles = (this->k_size + buffer_w - 1) / buffer_w;
    this->w_row_lefts = this->k_size % buffer_w;

    /* Z/Y matrix (M × K): tiles along rows and cols */
    this->z_col_tiles = this->x_col_tiles;
    this->z_col_lefts = this->x_col_lefts;
    this->z_row_tiles = this->w_row_tiles;
    this->z_row_lefts = this->w_row_lefts;

    /* Store-tile dimensions (last column of tiles may be smaller) */
    this->z_store_width  = (this->z_row_lefts > 0) ? this->z_row_lefts : buffer_w;
    this->z_store_height = (this->z_col_lefts > 0) ? this->z_col_lefts : buffer_h;

    /* Iteration state */
    this->iter_i = 0;
    this->iter_j = 0;
    this->iter_k = 0;

    /* Starting tile addresses */
    this->iter_x_addr = this->x_addr;
    this->iter_w_addr = this->w_addr;
    this->iter_y_addr = this->y_addr;
    this->iter_z_addr = this->z_addr;

    /* Iteration instruction state */
    this->iter_instruction = INSTR_LOAD_Y;
    this->iter_x_row_ptr  = 0;
    this->iter_w_row_ptr  = 0;
    this->iter_y_row_ptr  = 0;
    this->iter_z_row_ptr  = 0;

    /* Zero local buffers */
    memset(this->y_buffer_preload, 0, this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size);
    memset(this->w_buffer,         0, this->LOCAL_BUFFER_N * this->LOCAL_BUFFER_W * this->elem_size);
    memset(this->x_buffer,         0, this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_N * this->elem_size);
    memset(this->z_buffer_compute, 0, this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size);
    memset(this->z_buffer_previos, 0, this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size);

    /* Ideal runtime (used for utilization reporting) */
    this->ideal_runtime = (1.0 * this->m_size * this->n_size * this->k_size) /
                          (1.0 * this->ce_height * this->ce_width);
}

/* ------------------------------------------------------------------ */
/*  calculate_tile_base_address()                                     */
/*  Given a matrix base address, its stride (row width in elements),  */
/*  tile dimensions, and tile indices (i, j), returns the byte        */
/*  address of tile(i, j)'s top-left element.                         */
/* ------------------------------------------------------------------ */

uint32_t LightRedmule::calculate_tile_base_address(
    uint32_t base, uint32_t stride,
    uint32_t tile_col, uint32_t tile_row,
    uint32_t i, uint32_t j)
{
    return base + (i * tile_col * stride + j * tile_row) * this->elem_size;
}

/* ------------------------------------------------------------------ */
/*  inc_addr()                                                        */
/*  Advance an address by one row within a tile.                      */
/* ------------------------------------------------------------------ */

uint32_t LightRedmule::inc_addr(uint32_t addr, uint32_t stride, uint32_t tile_row)
{
    return addr + stride * this->elem_size;
}

/* ------------------------------------------------------------------ */
/*  next_iteration()                                                  */
/*  Advance (iter_i, iter_j, iter_k) to the next tile triple.        */
/*  Returns 0 if more iterations remain, 1 if all done.              */
/* ------------------------------------------------------------------ */

uint32_t LightRedmule::next_iteration()
{
    this->iter_k += 1;
    if (this->iter_k == this->x_row_tiles) {
        this->iter_k = 0;
        this->iter_j += 1;
        if (this->iter_j == this->z_row_tiles) {
            this->iter_j = 0;
            this->iter_i += 1;
            if (this->iter_i == this->z_col_tiles) {
                return 1;  /* All tiles processed */
            }
        }
    }
    /* Update z_store dimensions based on current tile position */
    this->z_store_width = ((this->iter_j == (this->z_row_tiles - 1)) && (this->z_row_lefts > 0))
        ? this->z_row_lefts
        : this->ce_width * (this->ce_pipe + 1);
    this->z_store_height = ((this->iter_i == (this->z_col_tiles - 1)) && (this->z_col_lefts > 0))
        ? this->z_col_lefts
        : this->ce_height;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  next_addr() / tmp_next_addr()                                     */
/*  Generate the next TCDM address for the current micro-instruction  */
/*  and advance the row pointer.                                      */
/* ------------------------------------------------------------------ */

uint32_t LightRedmule::tmp_next_addr()
{
    uint32_t addr = 0;
    uint32_t buffer_n = this->bandwidth / this->elem_size;

    switch (this->iter_instruction) {
        case INSTR_LOAD_Y:
            addr = this->iter_y_addr + this->iter_y_row_ptr * this->k_size * this->elem_size;
            break;
        case INSTR_LOAD_W:
        case INSTR_LOAD_W_COMPUTE:
            addr = this->iter_w_addr + this->iter_w_row_ptr * this->k_size * this->elem_size;
            break;
        case INSTR_LOAD_X:
            addr = this->iter_x_addr + this->iter_x_row_ptr * this->n_size * this->elem_size;
            break;
        case INSTR_STOR_Z:
            addr = this->iter_z_addr + this->iter_z_row_ptr * this->k_size * this->elem_size;
            break;
        default:
            this->trace.fatal("[LightRedmule] INVALID iter_instruction in tmp_next_addr: %d\n",
                              this->iter_instruction);
    }
    return addr;
}

uint32_t LightRedmule::next_addr()
{
    uint32_t addr = this->tmp_next_addr();

    /* Advance to next micro-instruction if we've exhausted the current block */
    switch (this->iter_instruction) {
        case INSTR_LOAD_Y:
            this->iter_y_row_ptr += 1;
            if (this->iter_y_row_ptr >= this->y_acc_block) {
                this->iter_y_row_ptr = 0;
                this->iter_instruction = INSTR_LOAD_W;
            }
            break;
        case INSTR_LOAD_W:
            this->iter_w_row_ptr += 1;
            if (this->iter_w_row_ptr >= this->w_acc_block) {
                this->iter_w_row_ptr = 0;
                this->iter_instruction = INSTR_LOAD_X;
            }
            break;
        case INSTR_LOAD_W_COMPUTE:
            this->iter_w_row_ptr += 1;
            if (this->iter_w_row_ptr >= this->w_acc_block) {
                this->iter_w_row_ptr = 0;
                if (this->x_acc_block > 0)
                    this->iter_instruction = INSTR_LOAD_X;
                else if (this->y_acc_block > 0)
                    this->iter_instruction = INSTR_LOAD_Y;
                else if (this->z_acc_block > 0)
                    this->iter_instruction = INSTR_STOR_Z;
                else
                    this->trace.fatal("[LightRedmule] INVALID next instruction after LOAD_W_COMPUTE\n");
            }
            break;
        case INSTR_LOAD_X:
            this->iter_x_row_ptr += 1;
            if (this->iter_x_row_ptr >= this->x_acc_block) {
                this->iter_x_row_ptr = 0;
                if (this->y_acc_block > 0)
                    this->iter_instruction = INSTR_LOAD_Y;
                else if (this->z_acc_block > 0)
                    this->iter_instruction = INSTR_STOR_Z;
                else
                    this->trace.fatal("[LightRedmule] INVALID next instruction after LOAD_X\n");
            }
            break;
        case INSTR_STOR_Z:
            this->iter_z_row_ptr += 1;
            if (this->iter_z_row_ptr >= this->z_acc_block) {
                this->iter_z_row_ptr = 0;
                this->iter_instruction = INSTR_LOAD_W_COMPUTE;
            }
            break;
        default:
            this->trace.fatal("[LightRedmule] INVALID iter_instruction in next_addr: %d\n",
                              this->iter_instruction);
    }

    return addr;
}

/* ------------------------------------------------------------------ */
/*  Block-number calculators for each FSM phase.                      */
/*  These determine how many TCDM requests to issue in PRELOAD,       */
/*  ROUTINE, and STORING.                                             */
/* ------------------------------------------------------------------ */

uint32_t LightRedmule::get_preload_access_block_number()
{
    uint32_t total_blocks = 0;
    this->x_acc_block = 0;
    this->w_acc_block = 0;
    this->y_acc_block = 0;
    this->z_acc_block = 0;

    /* PRELOAD loads initial X and Y tiles */
    this->x_acc_block = this->m_size < this->ce_height ? this->m_size : this->ce_height;
    this->y_acc_block = this->m_size < this->ce_height ? this->m_size : this->ce_height;
    total_blocks = this->x_acc_block + this->y_acc_block;
    return total_blocks;
}

uint32_t LightRedmule::get_routine_access_block_number()
{
    uint32_t total_blocks       = 0;
    uint32_t is_last_iteration  = (this->iter_i == (this->z_col_tiles - 1)) &&
                                  (this->iter_j == (this->z_row_tiles - 1)) &&
                                  (this->iter_k == (this->x_row_tiles - 1));
    uint32_t is_first_iteration = (this->iter_i == 0) && (this->iter_j == 0) && (this->iter_k == 0);
    uint32_t buffer_h  = this->ce_height;
    uint32_t buffer_w  = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n  = this->bandwidth / this->elem_size;
    uint32_t tcdms_bw  = buffer_n;

    this->x_acc_block = 0;
    this->w_acc_block = 0;
    this->y_acc_block = 0;
    this->z_acc_block = 0;

    /* W block */
    if (this->iter_k == (this->x_row_tiles - 1) && (this->x_row_lefts > 0))
        this->w_acc_block = this->x_row_lefts;
    else
        this->w_acc_block = tcdms_bw;
    total_blocks += this->w_acc_block;
    this->iter_w_addr = this->calculate_tile_base_address(this->w_addr, this->k_size, buffer_n, buffer_w, this->iter_k, this->iter_j);

    /* X block (prefetch for NEXT iteration) */
    if (is_last_iteration == 0) {
        uint32_t _k = this->iter_k, _j = this->iter_j, _i = this->iter_i;
        _k += 1;
        if (_k == this->x_row_tiles)  { _k = 0; _j += 1; if (_j == this->z_row_tiles) { _j = 0; _i += 1; } }
        this->iter_x_addr = this->calculate_tile_base_address(this->x_addr, this->n_size, buffer_h, buffer_n, _i, _k);
        this->x_acc_block = (this->m_size - _i * this->ce_height) < this->ce_height
            ? (this->m_size - _i * this->ce_height) : this->ce_height;
        total_blocks += this->x_acc_block;
    }

    /* Y block (prefetch for NEXT iteration, only when k wraps) */
    if (this->iter_k == (this->x_row_tiles - 1) && (is_last_iteration == 0)) {
        uint32_t _i = this->iter_i, _j = this->iter_j;
        _j += 1;
        if (_j == this->z_row_tiles) { _j = 0; _i += 1; }
        this->iter_y_addr = this->calculate_tile_base_address(this->y_addr, this->k_size, buffer_h, buffer_w, _i, _j);
        this->y_acc_block = (this->m_size - _i * this->ce_height) < this->ce_height
            ? (this->m_size - _i * this->ce_height) : this->ce_height;
        total_blocks += this->y_acc_block;
    }

    /* Z block (store PREVIOUS tile, only when k==0 and not first iteration) */
    if ((this->iter_k == 0) && (is_first_iteration == 0)) {
        this->z_acc_block = this->z_store_height;
        total_blocks += this->z_acc_block;
        uint32_t _i = this->iter_i, _j = this->iter_j;
        if (_j == 0) { _j = this->z_row_tiles - 1; _i -= 1; } else { _j -= 1; }
        this->iter_z_addr = this->calculate_tile_base_address(this->z_addr, this->k_size, buffer_h, buffer_w, _i, _j);
    }

    return total_blocks;
}

uint32_t LightRedmule::get_storing_access_block_number()
{
    uint32_t buffer_h  = this->ce_height;
    uint32_t buffer_w  = this->ce_width * (this->ce_pipe + 1);

    this->x_acc_block = 0;
    this->w_acc_block = 0;
    this->y_acc_block = 0;
    this->z_acc_block = 0;

    /* Store the final Z tile */
    this->z_acc_block = this->z_store_height;
    this->iter_z_addr = this->calculate_tile_base_address(
        this->z_addr, this->k_size, buffer_h, buffer_w,
        this->z_col_tiles - 1, this->z_row_tiles - 1);

    return this->z_acc_block;
}

uint32_t LightRedmule::get_routine_to_storing_latency()
{
    return this->ce_width * (this->ce_pipe + 1);
}

uint32_t LightRedmule::get_redmule_array_runtime()
{
    uint32_t tcdms_bw      = this->bandwidth / this->elem_size;
    uint32_t runtime_unit  = this->ce_width * (this->ce_pipe + 1);
    uint32_t runtime       = tcdms_bw * (this->ce_pipe + 1);

    if (this->iter_k == (this->x_row_tiles - 1) && (this->x_row_lefts > 0))
        runtime = this->x_row_lefts * (this->ce_pipe + 1);

    uint32_t runtime_pices = (runtime + runtime_unit - 1) / runtime_unit;
    return runtime_pices * runtime_unit;
}

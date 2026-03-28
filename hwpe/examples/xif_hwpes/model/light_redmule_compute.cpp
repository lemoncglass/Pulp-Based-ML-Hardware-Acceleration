/*
 * LightRedMulE — Compute Dispatch
 * =================================
 *
 * This file handles the per-row micro-instruction processing during
 * TCDM transfers.  While memory requests fly, the model simultaneously
 * fills/drains local buffers and kicks off the matmul kernel when a
 * complete tile has been loaded.
 *
 * Key functions:
 *   process_iter_instruction() — dispatches on the current iter_instruction
 *   process_compute()          — calls the right matmul kernel
 *
 * Buffer flow (per ROUTINE iteration):
 *
 *   TCDM ──load──► w_buffer    (weight tile, row by row)
 *   TCDM ──load──► x_buffer    (input tile, row by row)
 *   TCDM ──load──► y_buffer    ──copy──► z_buffer_compute
 *   z_buffer_compute = y + x·w  (via matmul kernel)
 *   z_buffer_compute ──store──► TCDM
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#include "light_redmule.h"
#include <cstring>

/* ------------------------------------------------------------------ */
/*  process_iter_instruction()                                        */
/*  Called once per TCDM access.  Depending on the current            */
/*  iter_instruction, it copies data between the access_buffer and    */
/*  the appropriate local buffer.                                     */
/* ------------------------------------------------------------------ */

void LightRedmule::process_iter_instruction()
{
    uint32_t buffer_h = this->ce_height;
    uint32_t buffer_w = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n = this->bandwidth / this->elem_size;

    switch (this->iter_instruction) {

        case INSTR_LOAD_Y: {
            /* Copy one row of Y from access_buffer → y_buffer_preload */
            uint32_t row = this->iter_y_row_ptr;
            uint32_t row_bytes = buffer_w * this->elem_size;
            memcpy(this->y_buffer_preload + row * row_bytes,
                   this->access_buffer,
                   row_bytes);
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Compute] LOAD_Y row=%d\n", row);
            break;
        }

        case INSTR_LOAD_W: {
            /* Copy one row of W from access_buffer → w_buffer */
            uint32_t row = this->iter_w_row_ptr;
            uint32_t row_bytes = buffer_w * this->elem_size;
            memcpy(this->w_buffer + row * row_bytes,
                   this->access_buffer,
                   row_bytes);
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Compute] LOAD_W row=%d\n", row);
            break;
        }

        case INSTR_LOAD_W_COMPUTE: {
            /* Load W row AND trigger compute once the last row arrives */
            uint32_t row = this->iter_w_row_ptr;
            uint32_t row_bytes = buffer_w * this->elem_size;
            memcpy(this->w_buffer + row * row_bytes,
                   this->access_buffer,
                   row_bytes);
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Compute] LOAD_W_COMPUTE row=%d\n", row);

            /* When all W rows loaded, run the matmul */
            if (row == this->w_acc_block - 1) {
                this->process_compute();
            }
            break;
        }

        case INSTR_LOAD_X: {
            /* Copy one row of X from access_buffer → x_buffer */
            uint32_t row = this->iter_x_row_ptr;
            uint32_t row_bytes = buffer_n * this->elem_size;
            memcpy(this->x_buffer + row * row_bytes,
                   this->access_buffer,
                   row_bytes);
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Compute] LOAD_X row=%d\n", row);
            break;
        }

        case INSTR_STOR_Z: {
            /* Copy one row of Z from z_buffer_previos → access_buffer (for TCDM write) */
            uint32_t row = this->iter_z_row_ptr;
            uint32_t row_bytes = buffer_w * this->elem_size;
            memcpy(this->access_buffer,
                   this->z_buffer_previos + row * row_bytes,
                   row_bytes);

            /* Mark the TCDM request as a write */
            this->tcdm_req->set_is_write(true);
            this->tcdm_req->set_size(row_bytes);
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Compute] STOR_Z row=%d\n", row);
            break;
        }

        case INSTR_FORWARD_YZ: {
            /* Copy y_buffer_preload → z_buffer_compute to start new accumulation.
             * Then swap z_buffer_compute ↔ z_buffer_previos so the just-computed
             * tile is ready for the next STOR_Z phase. */
            uint32_t buf_size = this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size;
            memcpy(this->z_buffer_previos, this->z_buffer_compute, buf_size);
            memcpy(this->z_buffer_compute, this->y_buffer_preload, buf_size);
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[LightRedmule][Compute] FORWARD_YZ\n");
            break;
        }

        default:
            this->trace.fatal("[LightRedmule] INVALID iter_instruction: %d\n",
                              this->iter_instruction);
    }
}

/* ------------------------------------------------------------------ */
/*  process_compute()                                                 */
/*  Calls the appropriate matmul kernel based on compute_able.        */
/*  The kernel reads from x_buffer, w_buffer, z_buffer_compute and    */
/*  writes the result back to z_buffer_compute (in-place accumulate). */
/* ------------------------------------------------------------------ */

void LightRedmule::process_compute()
{
    uint32_t buffer_h = this->ce_height;
    uint32_t buffer_w = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n = this->bandwidth / this->elem_size;

    /* Determine actual tile dimensions (may be smaller at boundaries) */
    uint32_t m = (this->m_size - this->iter_i * buffer_h) < buffer_h
        ? (this->m_size - this->iter_i * buffer_h) : buffer_h;
    uint32_t n = (this->iter_k == (this->x_row_tiles - 1) && this->x_row_lefts > 0)
        ? this->x_row_lefts : buffer_n;
    uint32_t k = (this->iter_j == (this->z_row_tiles - 1) && this->z_row_lefts > 0)
        ? this->z_row_lefts : buffer_w;

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[LightRedmule][Compute] matmul [%d] m=%d n=%d k=%d\n",
        this->compute_able, m, n, k);

    switch (this->compute_able) {
        case 1:
            matmul_uint16((uint16_t *)this->z_buffer_compute,
                          (uint16_t *)this->z_buffer_compute,
                          (uint16_t *)this->x_buffer,
                          (uint16_t *)this->w_buffer,
                          m, n, k);
            break;
        case 2:
            matmul_int16((int16_t *)this->z_buffer_compute,
                         (int16_t *)this->z_buffer_compute,
                         (int16_t *)this->x_buffer,
                         (int16_t *)this->w_buffer,
                         m, n, k);
            break;
        case 3:
            matmul_fp16((fp16 *)this->z_buffer_compute,
                        (fp16 *)this->z_buffer_compute,
                        (fp16 *)this->x_buffer,
                        (fp16 *)this->w_buffer,
                        m, n, k);
            break;
        case 5:
            matmul_uint8((uint8_t *)this->z_buffer_compute,
                         (uint8_t *)this->z_buffer_compute,
                         (uint8_t *)this->x_buffer,
                         (uint8_t *)this->w_buffer,
                         m, n, k);
            break;
        case 6:
            matmul_int8((int8_t *)this->z_buffer_compute,
                        (int8_t *)this->z_buffer_compute,
                        (int8_t *)this->x_buffer,
                        (int8_t *)this->w_buffer,
                        m, n, k);
            break;
        case 7:
            matmul_fp8e4m3((fp8e4m3 *)this->z_buffer_compute,
                           (fp8e4m3 *)this->z_buffer_compute,
                           (fp8e4m3 *)this->x_buffer,
                           (fp8e4m3 *)this->w_buffer,
                           m, n, k);
            break;
        default:
            this->trace.fatal("[LightRedmule] INVALID compute_able: %d\n",
                              this->compute_able);
    }
}

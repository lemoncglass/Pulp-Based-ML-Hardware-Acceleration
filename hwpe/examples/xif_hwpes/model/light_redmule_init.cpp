/*
 * LightRedMulE — Constructor & Initialization
 * ==============================================
 *
 * This file contains:
 *   1. The gv_new() factory function (GVSoC entry point for creating this component)
 *   2. The LightRedmule constructor — sets up all GVSoC ports, reads config
 *      from the JSON tree, allocates local buffers, and initializes the FSM.
 *
 * GVSoC Port Wiring Summary (see also magia/tile.py for full wiring):
 *
 *   input_itf      ← IoSlave   : MMIO config register access from the core
 *   offload_itf    ← WireSlave : XIF instruction offload from XifDecoder
 *   offload_grant  → WireMaster: Grant/result sent back through XifDecoder to core
 *   done           → WireMaster: IRQ line (active-high pulse when compute finishes)
 *   tcdm_itf       → IoMaster  : Memory port to TCDM (via HWPEInterleaver)
 *
 * Original monolithic file: gvsoc/pulp/pulp/light_redmule/light_redmule.cpp
 */

#include "light_redmule.h"

/* ------------------------------------------------------------------ */
/*  GVSoC factory — called by the framework to instantiate this       */
/*  component.  The Python wrapper (light_redmule.py) points at       */
/*  this .cpp via add_sources(); GVSoC compiles it into a .so and     */
/*  loads gv_new at simulation start.                                 */
/* ------------------------------------------------------------------ */

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new LightRedmule(config);
}

/* ------------------------------------------------------------------ */
/*  Constructor                                                       */
/* ------------------------------------------------------------------ */

LightRedmule::LightRedmule(vp::ComponentConf &config)
    : vp::Component(config)
{
    /* ---- Trace channel ---- */
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    /* ---- MMIO slave port ---- */
    this->input_itf.set_req_meth(&LightRedmule::req);
    this->new_slave_port("input", &this->input_itf);

    /* ---- TCDM master port (memory access) ---- */
    this->new_master_port("tcdm", &this->tcdm_itf);

    /* ---- XIF offload slave (receives custom instructions) ---- */
    this->offload_itf.set_sync_meth(&LightRedmule::offload_sync);
    this->new_slave_port("offload", &this->offload_itf, this);

    /* ---- XIF grant master (sends writeback/unstall to core) ---- */
    this->new_master_port("offload_grant", &this->offload_grant_itf, this);

    /* ---- IRQ master (active-high when done) ---- */
    this->new_master_port("done_irq", &this->done, this);

    /* ================================================================
     *  Read hardware configuration from the JSON config tree.
     *  These values are set in the Python wrapper (light_redmule.py)
     *  via add_properties({...}).
     * ================================================================ */

    this->tcdm_bank_width   = get_js_config()->get("tcdm_bank_width")->get_int();
    this->tcdm_bank_number  = get_js_config()->get("tcdm_bank_number")->get_int();
    this->elem_size         = get_js_config()->get("elem_size")->get_int();
    this->ce_height         = get_js_config()->get("ce_height")->get_int();
    this->ce_width          = get_js_config()->get("ce_width")->get_int();
    this->ce_pipe           = get_js_config()->get("ce_pipe")->get_int();
    this->queue_depth       = get_js_config()->get("queue_depth")->get_int();
    this->fold_tiles_mapping= get_js_config()->get("fold_tiles_mapping")->get_int();
    this->loc_base          = get_js_config()->get("loc_base")->get_double();
    this->compute_able      = 0;

    /* Derived constants */
    this->bandwidth         = this->tcdm_bank_width * this->tcdm_bank_number;
    this->LOCAL_BUFFER_H    = this->ce_height;
    this->LOCAL_BUFFER_N    = this->bandwidth / this->elem_size;
    this->LOCAL_BUFFER_W    = this->ce_width * (this->ce_pipe + 1);

    /* ---- Initialize software-visible registers (defaults) ---- */
    this->m_size    = 4;  this->n_size = 4;  this->k_size = 4;
    this->x_addr    = 0;  this->w_addr = 0;
    this->y_addr    = 0;  this->z_addr = 0;

    /* ---- Zero all tiling metadata ---- */
    this->x_row_tiles = 0; this->x_row_lefts = 0;
    this->x_col_tiles = 0; this->x_col_lefts = 0;
    this->w_row_tiles = 0; this->w_row_lefts = 0;
    this->w_col_tiles = 0; this->w_col_lefts = 0;
    this->z_row_tiles = 0; this->z_row_lefts = 0;
    this->z_col_tiles = 0; this->z_col_lefts = 0;
    this->iter_i = 0; this->iter_j = 0; this->iter_k = 0;
    this->iter_x_addr = 0; this->iter_w_addr = 0;
    this->iter_y_addr = 0; this->iter_z_addr = 0;
    this->x_acc_block = 0; this->w_acc_block = 0;
    this->y_acc_block = 0; this->z_acc_block = 0;
    this->z_store_width  = 0;
    this->z_store_height = 0;
    this->ideal_runtime  = 0;
    this->iter_instruction = INSTR_LOAD_Y;
    this->iter_x_row_ptr = 0; this->iter_w_row_ptr = 0;
    this->iter_y_row_ptr = 0; this->iter_z_row_ptr = 0;

    /* ---- Allocate local compute buffers ---- */
    this->access_buffer    = new uint8_t[this->bandwidth * 2];
    this->y_buffer_preload = new uint8_t[this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size];
    this->w_buffer         = new uint8_t[this->LOCAL_BUFFER_N * this->LOCAL_BUFFER_W * this->elem_size];
    this->x_buffer         = new uint8_t[this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_N * this->elem_size];
    this->z_buffer_compute = new uint8_t[this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size];
    this->z_buffer_previos = new uint8_t[this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size];

    /* ---- Initialize FSM ---- */
    this->state.set(IDLE);
    this->redmule_query    = NULL;
    this->tcdm_req         = this->tcdm_itf.req_new(0, 0, 0, 0);
    this->fsm_event        = this->event_new(&LightRedmule::fsm_handler);
    this->tcdm_block_total = 0;
    this->fsm_counter      = 0;
    this->fsm_timestamp    = 0;
    this->timer_start      = 0;
    this->cycle_start      = 0;
    this->total_runtime    = 0;
    this->num_matmul       = 0;

    this->trace.msg("[LightRedmule] Model Initialization Done!\n");
}

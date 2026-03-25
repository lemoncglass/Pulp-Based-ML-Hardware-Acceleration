/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Toy Adder HWPE — GVSoC simulation model (skeleton)
 *
 * This file implements the cycle-accurate C++ model that GVSoC loads
 * when the toy_adder accelerator is enabled in the target config.
 *
 * === How this file works (we'll fill each section in together) ===
 *
 * SECTION 1 — Includes & constants
 *   Pull in the GVSoC component API and define the register offsets
 *   (must match archi_toy_adder.h from the test side).
 *
 * SECTION 2 — Class definition
 *   Inherits from vp::Component.  Declares:
 *     - ports   : one slave (MMIO from CPU), one master (memory access),
 *                 one master (IRQ line to event unit)
 *     - handler : static method called by GVSoC when the CPU reads/writes
 *                 our address range
 *     - state   : register file, FSM state, clock events
 *
 * SECTION 3 — Constructor
 *   Creates the trace channel, ports, sets the MMIO handler, allocates
 *   clock events for the FSM.
 *
 * SECTION 4 — MMIO handler  (hwpe_slave)
 *   Dispatches incoming reads/writes:
 *     offset < 0x20  → control registers (trigger, acquire, status …)
 *     offset >= 0x40 → job registers (operand A, operand B, result ptr)
 *
 * SECTION 5 — FSM / computation
 *   When TRIGGER is written:
 *     1. Read operand A and B from memory via the master port
 *     2. Compute result = A + B
 *     3. Write result back to memory
 *     4. Fire the IRQ to wake up the core
 *
 * SECTION 6 — Factory function
 *   Required by GVSoC's plugin loader.  Must be extern "C".
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * SECTION 1 — Register offsets (must match archi_toy_adder.h)        *
 * ------------------------------------------------------------------ */

// Control registers
#define TOY_ADDER_TRIGGER       0x00
#define TOY_ADDER_ACQUIRE       0x04
#define TOY_ADDER_FINISHED      0x08
#define TOY_ADDER_STATUS        0x0C
#define TOY_ADDER_RUNNING_JOB   0x10
#define TOY_ADDER_SOFT_CLEAR    0x14

// Job registers (offsets relative to REG_OFFS)
#define TOY_ADDER_REG_OFFS      0x40
#define TOY_ADDER_REG_A_PTR     0x00 // will always be called as (REG_OFFS + <offset>)
#define TOY_ADDER_REG_B_PTR     0x04 // ^
#define TOY_ADDER_REG_RES_PTR   0x08 // ^

/* ------------------------------------------------------------------ *
 * SECTION 2 — Class definition                                       *
 * ------------------------------------------------------------------ */

class ToyAdder : public vp::Component
{
public:
    ToyAdder(vp::ComponentConf &config);

    void reset(bool active) override;

    // MMIO handler — called by GVSoC when the CPU accesses our address range
    static vp::IoReqStatus hwpe_slave(vp::Block *__this, vp::IoReq *req);

    // FSM handler — called on clock events to perform the computation
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    // Helper — issue a 4-byte read or write through the master port
    //   addr:     L1 address to access
    //   data:     pointer to the local buffer (filled on read, sent on write)
    //   is_write: false = read, true = write
    //   Returns true on success, false on error.
    bool access_mem(uint32_t addr, uint8_t *data, bool is_write);

private:
    vp::Trace       trace;          // Debug/trace channel

    vp::IoSlave     in;             // Slave port  — MMIO from CPU
    vp::IoMaster    out;            // Master port — read/write L1 memory
    vp::WireMaster<bool> irq;       // Master port — interrupt to event unit

    vp::IoReq       io_req;         // Reusable request object for memory reads/writes
    vp::ClockEvent *fsm_event;      // Clock event for the computation FSM

    // Register file (job-specific registers)
    uint32_t reg_a_ptr;             // Address of operand A in L1
    uint32_t reg_b_ptr;             // Address of operand B in L1
    uint32_t reg_res_ptr;           // Address where result is written

    // FSM state
    enum { IDLE, RUNNING } state;
};

/* ------------------------------------------------------------------ *
 * SECTION 3 — Constructor                                            *
 * ------------------------------------------------------------------ */

ToyAdder::ToyAdder(vp::ComponentConf &config) : vp::Component(config)
{
    // 1. Debug trace channel — lets us print messages with this->trace.msg(...)
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // 2. Master port to L1 memory (name "out" must match o_OUT in toy_adder.py)
    this->new_master_port("out", &this->out);

    // 3. Master port for the interrupt line (name "irq" must match o_IRQ)
    this->new_master_port("irq", &this->irq);

    // 4. Set the MMIO handler, then register the slave port
    //    (name "input" must match i_INPUT in toy_adder.py)
    this->in.set_req_meth(&ToyAdder::hwpe_slave);
    this->new_slave_port("input", &this->in);

    // 5. Allocate the clock event for the computation FSM
    this->fsm_event = this->event_new(&ToyAdder::fsm_handler);

    this->trace.msg("ToyAdder build complete\n");
}

/* ------------------------------------------------------------------ *
 * Reset                                                              *
 * ------------------------------------------------------------------ */

void ToyAdder::reset(bool active)
{
    if (active)
    {
        this->state = IDLE;
        this->reg_a_ptr   = 0;
        this->reg_b_ptr   = 0;
        this->reg_res_ptr = 0;
    }
}

/* ------------------------------------------------------------------ *
 * Helper — issue a 4-byte memory access through the master port      *
 * ------------------------------------------------------------------ */

bool ToyAdder::access_mem(uint32_t addr, uint8_t *data, bool is_write)
{
    this->io_req.init();
    this->io_req.set_addr(addr);
    this->io_req.set_size(4);
    this->io_req.set_data(data);
    this->io_req.set_is_write(is_write);

    if (this->out.req(&this->io_req) != vp::IO_REQ_OK)
    {
        this->trace.fatal("Memory access failed at 0x%x (%s)\n",
                          addr, is_write ? "write" : "read");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ *
 * SECTION 4 — MMIO handler                                           *
 * ------------------------------------------------------------------ */

vp::IoReqStatus ToyAdder::hwpe_slave(vp::Block *__this, vp::IoReq *req)
{
    // Cast the generic Block pointer back to our concrete type.
    // GVSoC calls us with a Block* because the handler is a static method;
    // we need the cast to access our member variables.
    ToyAdder *_this = (ToyAdder *)__this;

    uint32_t offset = req->get_addr();           // byte offset within our 0x400 region
    bool     is_write = req->get_is_write();
    uint32_t *data = (uint32_t *)req->get_data(); // pointer to the 4-byte data payload

    if (is_write)
    {
        uint32_t value = *data;

        // WRITING DATA
        if (offset >= TOY_ADDER_REG_OFFS)
        {
            // ---- Job registers (offset >= 0x40) ----
            uint32_t reg = offset - TOY_ADDER_REG_OFFS;  // relative offset

            switch (reg)
            {
                case TOY_ADDER_REG_A_PTR:
                    _this->trace.msg("Write REG_A_PTR = 0x%x\n", value);
                    _this->reg_a_ptr = value;
                    break;

                case TOY_ADDER_REG_B_PTR:
                    _this->trace.msg("Write REG_B_PTR = 0x%x\n", value);
                    _this->reg_b_ptr = value;
                    break;

                case TOY_ADDER_REG_RES_PTR:
                    _this->trace.msg("Write REG_RES_PTR = 0x%x\n", value);
                    _this->reg_res_ptr = value;
                    break;

                default:
                    _this->trace.msg("Write to unknown job reg 0x%x\n", offset);
                    break;
            }
        }
        else
        {
            //  WRITING CONTROL STATES
            // ---- Control registers (offset < 0x40) ----
            switch (offset)
            {
                case TOY_ADDER_TRIGGER: // Set state to running
                    _this->trace.msg("TRIGGER — starting job\n");
                    _this->state = RUNNING;
                    // Schedule the FSM to fire 1 cycle from now
                    _this->event_enqueue(_this->fsm_event, 1);
                    break;

                case TOY_ADDER_SOFT_CLEAR: // Reset everything
                    _this->trace.msg("SOFT_CLEAR\n");
                    _this->state = IDLE;
                    _this->reg_a_ptr   = 0;
                    _this->reg_b_ptr   = 0;
                    _this->reg_res_ptr = 0;
                    break;
                
                // Writes to read-only control regs are silently ignored
                case TOY_ADDER_ACQUIRE:     break;
                case TOY_ADDER_FINISHED:    break;
                case TOY_ADDER_STATUS:      break;
                case TOY_ADDER_RUNNING_JOB: break;

                default:
                    _this->trace.msg("Attempted write to unknown ctrl reg 0x%x\n", offset);
                    break;
            }
        }
    }
    else
    {
        // READING STATUS, and DATA
        // ---- Read path ----
        switch (offset)
        {
            case TOY_ADDER_ACQUIRE: // can I accept a job request?
                // 0 = free (job slot available), nonzero = busy
                *data = (_this->state == IDLE) ? 0 : 1;
                _this->trace.msg("Read ACQUIRE → %d\n", *data);
                break;

            case TOY_ADDER_STATUS: // what state am I in?
                // 0 = idle, 1 = running
                *data = (_this->state == RUNNING) ? 1 : 0;
                _this->trace.msg("Read STATUS → %d\n", *data);
                break;

            case TOY_ADDER_FINISHED: // tell core I'm done with all jobs
                // 1 = finished, 0 = not finished
                *data = (_this->state == IDLE) ? 1 : 0;
                _this->trace.msg("Read FINISHED → %d\n", *data);
                break;

            default: // read from an unknown reg
                *data = 0;
                _this->trace.msg("Read from unknown reg 0x%x\n", offset);
                break;
        }
    }

    return vp::IO_REQ_OK;
}

/* ------------------------------------------------------------------ *
 * SECTION 5 — FSM / computation handler                              *
 * ------------------------------------------------------------------ */

void ToyAdder::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ToyAdder *_this = (ToyAdder *)__this;

    int32_t operand_a, operand_b, result;

    // ---- 1. Read operand A from L1 memory ----
    if (_this->access_mem(_this->reg_a_ptr, (uint8_t *)&operand_a, false)){ // READ        // a negative check + return is more defensive, but this is more readable for the toy example    
        _this->trace.msg("Read operand A = %d from 0x%x\n", operand_a, _this->reg_a_ptr);  // Log for debugging (if failed, the trace fatal killed it and we won't get here)
    }

    // ---- 2. Read operand B from L1 memory ----
    if (_this->access_mem(_this->reg_b_ptr, (uint8_t *)&operand_b, false)){ // READ
        _this->trace.msg("Read operand B = %d from 0x%x\n", operand_b, _this->reg_b_ptr);  // Log for debugging
    }


    // ---- 3. Compute ---- //
    result = operand_a + operand_b;
    _this->trace.msg("Computed %d + %d = %d\n", operand_a, operand_b, result); // Log for debugging
    // -------------------- //


    // ---- 4. Write result back to L1 memory ----
    if (_this->access_mem(_this->reg_res_ptr, (uint8_t *)&result, true)){ // WRITE
        _this->trace.msg("Wrote result %d to 0x%x\n", result, _this->reg_res_ptr); // Log for debugging
    }

    
    // ---- 5. Signal completion ----
    _this->state = IDLE;
    _this->irq.sync(true);          // assert IRQ line to wake up the core
    _this->trace.msg("Job done — IRQ fired\n");
}

/* ------------------------------------------------------------------ *
 * SECTION 6 — Factory function (required by GVSoC)                   *
 * ------------------------------------------------------------------ */

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ToyAdder(config);
}

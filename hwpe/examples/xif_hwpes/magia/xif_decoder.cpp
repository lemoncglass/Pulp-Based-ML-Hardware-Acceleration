/*
 * XIF Decoder — Opcode Router (Reference Copy)
 * ===============================================
 *
 * This component sits between the CV32E40X core and all XIF-connected
 * coprocessors (RedMulE, iDMA controller, FractalSync).  It receives
 * custom instructions via the offload wire from the core and routes
 * them to the correct downstream slave based on the opcode[6:0] field.
 *
 * Routing table:
 *   0b0001011 (custom-0) → RedMulE   (S2)   — mcnfig instruction
 *   0b0101011 (custom-1) → RedMulE   (S2)   — marith instruction
 *   0b1111011            → iDMA      (S1)   — DMA transfer instructions
 *   0b1011011 + func3=010→ FractalSync       — barrier instruction
 *   0b1011011 (other)    → iDMA      (S1)   — DMA config instructions
 *
 * Grants flow back upstream: when a coprocessor grants an instruction,
 * the XifDecoder forwards the grant to the core via offload_grant_itf_m.
 *
 * Original file: gvsoc/pulp/pulp/chips/magia/xif_decoder/xif_decoder.cpp
 * Author: Lorenzo Zuolo (Chips-IT)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <stdint.h>

#include <cpu/iss/include/offload.hpp>
#include "../fractal_sync/fractal_sync.hpp"

enum fractal_directions {
    EAST_WEST,            // horizontal = 0
    NORD_SUD,             // vertical = 1
    NEIGHBOUR_EAST_WEST,  // neighbour_horizontal = 2
    NEIGHBOUR_NORD_SUD    // neighbour_vertical = 3
};


class XifDecoder : public vp::Component
{
public:
    XifDecoder(vp::ComponentConf &config);

protected:
    /* Offload master port — receives instructions from the core */
    static void offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf_m;
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_m;

    /* Slave port S1 — iDMA controller */
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_s1;
    static void grant_sync_s1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_s1;

    /* Slave port S2 — RedMulE */
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_s2;
    static void grant_sync_s2(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_s2;

    /* FractalSync ports */
    static void fractal_output_method(vp::Block *__this, PortResp<uint32_t> *req, int id);
    vp::WireMaster<PortReq<uint32_t> *> fractal_ew_input_port;
    vp::WireSlave<PortResp<uint32_t> *> fractal_ew_output_port;
    vp::WireMaster<PortReq<uint32_t> *> fractal_ns_input_port;
    vp::WireSlave<PortResp<uint32_t> *> fractal_ns_output_port;
    vp::WireMaster<PortReq<uint32_t> *> neighbour_fractal_ew_input_port;
    vp::WireSlave<PortResp<uint32_t> *> neighbour_fractal_ew_output_port;
    vp::WireMaster<PortReq<uint32_t> *> neighbour_fractal_ns_input_port;
    vp::WireSlave<PortResp<uint32_t> *> neighbour_fractal_ns_output_port;

    vp::Trace trace;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new XifDecoder(config);
}

XifDecoder::XifDecoder(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Master offload port (from core)
    this->offload_itf_m.set_sync_meth(&XifDecoder::offload_sync_m);
    this->new_slave_port("offload_m", &this->offload_itf_m, this);
    this->new_master_port("offload_grant_m", &this->offload_grant_itf_m, this);

    // Slave S1 (iDMA)
    this->new_master_port("offload_s1", &this->offload_itf_s1, this);
    this->offload_grant_itf_s1.set_sync_meth(&XifDecoder::grant_sync_s1);
    this->new_slave_port("offload_grant_s1", &this->offload_grant_itf_s1, this);

    // Slave S2 (RedMulE)
    this->new_master_port("offload_s2", &this->offload_itf_s2, this);
    this->offload_grant_itf_s2.set_sync_meth(&XifDecoder::grant_sync_s2);
    this->new_slave_port("offload_grant_s2", &this->offload_grant_itf_s2, this);

    // FractalSync ports
    this->fractal_ew_output_port.set_sync_meth_muxed(&XifDecoder::fractal_output_method, EAST_WEST);
    this->fractal_ns_output_port.set_sync_meth_muxed(&XifDecoder::fractal_output_method, NORD_SUD);
    this->neighbour_fractal_ew_output_port.set_sync_meth_muxed(&XifDecoder::fractal_output_method, NEIGHBOUR_EAST_WEST);
    this->neighbour_fractal_ns_output_port.set_sync_meth_muxed(&XifDecoder::fractal_output_method, NEIGHBOUR_NORD_SUD);
    this->new_slave_port("fractal_ew_output_port", &this->fractal_ew_output_port, this);
    this->new_slave_port("fractal_ns_output_port", &this->fractal_ns_output_port, this);
    this->new_slave_port("neighbour_fractal_ew_output_port", &this->neighbour_fractal_ew_output_port, this);
    this->new_slave_port("neighbour_fractal_ns_output_port", &this->neighbour_fractal_ns_output_port, this);
    this->new_master_port("fractal_ew_input_port", &this->fractal_ew_input_port, this);
    this->new_master_port("fractal_ns_input_port", &this->fractal_ns_input_port, this);
    this->new_master_port("neighbour_fractal_ew_input_port", &this->neighbour_fractal_ew_input_port, this);
    this->new_master_port("neighbour_fractal_ns_input_port", &this->neighbour_fractal_ns_input_port, this);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "[XifDecoder] Instantiated\n");
}

/* Grant callbacks — just forward upstream to core */

void XifDecoder::grant_sync_s1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result) {
    XifDecoder *_this = (XifDecoder *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "[XifDecoder] GRANT from iDMA\n");
    _this->offload_grant_itf_m.sync(result);
}

void XifDecoder::grant_sync_s2(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result) {
    XifDecoder *_this = (XifDecoder *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "[XifDecoder] GRANT from RedMulE\n");
    _this->offload_grant_itf_m.sync(result);
}

void XifDecoder::fractal_output_method(vp::Block *__this, PortResp<uint32_t> *req, int id) {
    XifDecoder *_this = (XifDecoder *)__this;
    if ((req->wake) && (!req->error)) {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "[XifDecoder] wake from Fractal dir=%d [id=%d aggr=0x%08x]\n", id, req->id_rsp, req->lvl);
        IssOffloadInsnGrant<uint32_t> offload_grant = { .result = 0x0 };
        _this->offload_grant_itf_m.sync(&offload_grant);
    } else if (req->error) {
        _this->trace.fatal("[XifDecoder] error from Fractal\n");
    }
}

/* ================================================================
 *  THE MAIN ROUTING FUNCTION
 *  This is where every custom instruction from the core gets
 *  dispatched to the correct coprocessor.
 * ================================================================ */

void XifDecoder::offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    XifDecoder *_this = (XifDecoder *)__this;
    uint32_t opc   = insn->opcode & 0x7F;
    uint32_t func3 = (insn->opcode >> 12) & 0x7;

    switch (opc)
    {
        case 0b1111011:  /* iDMA transfer instructions */
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "[XifDecoder] → iDMA\n");
            _this->offload_itf_s1.sync(insn);
            break;

        case 0b1011011:  /* iDMA config OR FractalSync */
            if (func3 == 0b010) {
                /* FractalSync barrier */
                _this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "[XifDecoder] → FractalSync (id=%d aggr=%d)\n", insn->arg_b, insn->arg_a);
                insn->granted = false;  /* stall core until barrier completes */
                PortReq<uint32_t> req = { .sync = true, .aggr = insn->arg_a, .id_req = insn->arg_b };
                if (req.aggr == 0b1) {
                    if      (req.id_req == 0) _this->fractal_ew_input_port.sync(&req);
                    else if (req.id_req == 1) _this->fractal_ns_input_port.sync(&req);
                    else if (req.id_req == 2) _this->neighbour_fractal_ew_input_port.sync(&req);
                    else if (req.id_req == 3) _this->neighbour_fractal_ns_input_port.sync(&req);
                    else _this->trace.fatal("[XifDecoder] wrong direction with aggr=0b1");
                } else {
                    if (req.id_req % 2 == 0)
                        _this->fractal_ew_input_port.sync(&req);
                    else
                        _this->fractal_ns_input_port.sync(&req);
                }
            } else {
                /* iDMA config */
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[XifDecoder] → iDMA (config)\n");
                _this->offload_itf_s1.sync(insn);
            }
            break;

        case 0b0001011:  /* mcnfig → RedMulE */
        case 0b0101011:  /* marith → RedMulE */
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "[XifDecoder] → RedMulE\n");
            _this->offload_itf_s2.sync(insn);
            break;

        default:
            _this->trace.fatal("[XifDecoder] Unknown opcode 0x%02x\n", opc);
            break;
    }
}

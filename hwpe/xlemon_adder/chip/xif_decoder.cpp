/*
 * XLemon XIF Decoder — Simplified Opcode Router
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 University of Missouri - Kansas City
 *
 * Author: Cody Glassbrenner <glassbrennercody@gmail.com>
 *         (heavily assisted by Claude Opus 4.6 agent)
 *
 * Simplified version of Magia's XifDecoder with:
 *   - 1 slave slot (S1) for the XLemonAdder
 *   - No FractalSync ports
 *   - No iDMA ports
 *
 * Routing table:
 *   opcode[6:0] == 0b0101011 (custom-1) → S1 (XLemonAdder)
 *   anything else → fatal error
 *
 * The XifDecoder sits between the core and the accelerator:
 *
 *   CV32 core  ──offload──►  XifDecoder  ──offload_s1──►  XLemonAdder
 *              ◄──grant───               ◄──grant_s1───
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <stdint.h>

#include <cpu/iss/include/offload.hpp>


class XLemonXifDecoder : public vp::Component
{
public:
    XLemonXifDecoder(vp::ComponentConf &config);

protected:
    /* Master port — receives instructions from the core */
    static void offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf_m;
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_m;

    /* Slave port S1 — XLemonAdder */
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_s1;
    static void grant_sync_s1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_s1;

    vp::Trace trace;
};


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new XLemonXifDecoder(config);
}


XLemonXifDecoder::XLemonXifDecoder(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    /* Core-facing master port */
    this->offload_itf_m.set_sync_meth(&XLemonXifDecoder::offload_sync_m);
    this->new_slave_port("offload_m", &this->offload_itf_m, this);
    this->new_master_port("offload_grant_m", &this->offload_grant_itf_m, this);

    /* S1 — XLemonAdder */
    this->new_master_port("offload_s1", &this->offload_itf_s1, this);
    this->offload_grant_itf_s1.set_sync_meth(&XLemonXifDecoder::grant_sync_s1);
    this->new_slave_port("offload_grant_s1", &this->offload_grant_itf_s1, this);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "[XLemonXifDecoder] Instantiated\n");
}


/* Grant from XLemonAdder → forward to core */
void XLemonXifDecoder::grant_sync_s1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result)
{
    XLemonXifDecoder *_this = (XLemonXifDecoder *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "[XLemonXifDecoder] GRANT from XLemonAdder\n");
    _this->offload_grant_itf_m.sync(result);
}


/* Main routing function — dispatch custom instructions */
void XLemonXifDecoder::offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    XLemonXifDecoder *_this = (XLemonXifDecoder *)__this;
    uint32_t opc = insn->opcode & 0x7F;

    switch (opc)
    {
        case 0b0101011:  /* custom-1 → XLemonAdder (xladd) */
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[XLemonXifDecoder] → XLemonAdder (xladd)\n");
            _this->offload_itf_s1.sync(insn);
            break;

        default:
            _this->trace.fatal(
                "[XLemonXifDecoder] Unknown opcode 0x%02x\n", opc);
            break;
    }
}

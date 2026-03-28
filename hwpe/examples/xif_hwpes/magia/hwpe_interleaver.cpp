/*
 * HWPE Interleaver — TCDM Bank Interleaver (Reference Copy)
 * ============================================================
 *
 * This component sits between the RedMulE accelerator's TCDM master port
 * and the actual TCDM memory banks.  It performs bank interleaving:
 * given a flat byte address from RedMulE, it computes which bank to
 * access and what offset within that bank.
 *
 * The interleaving scheme uses the low address bits to select the bank:
 *   bank_id = (addr >> log2(bank_width)) & (nb_banks - 1)
 *   bank_offset = upper bits || lowest bits (skipping the bank-select field)
 *
 * For a request larger than one bank width (e.g., a full cache line),
 * the interleaver splits it into per-bank sub-requests, sends them in
 * parallel, and returns the maximum latency.
 *
 * Original file: gvsoc/pulp/pulp/light_redmule/hwpe_interleaver.cpp
 * Author: Germain Haugou (GreenWaves Technologies)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <math.h>

class HWPEInterleaver : public vp::Component
{
public:
    HWPEInterleaver(vp::ComponentConf &config);
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    vp::Trace trace;
    std::vector<vp::IoMaster> output_ports;
    vp::IoSlave input_port;

    int id_shift;            /* log2(bank_width) — bits to skip for bank select */
    uint64_t id_mask;        /* (nb_banks - 1) — mask for bank ID extraction */
    int offset_right_shift;  /* log2(bank_width) + log2(nb_banks) */
    int offset_left_shift;   /* log2(bank_width) */
    int bank_width;
};

HWPEInterleaver::HWPEInterleaver(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    int nb_banks   = this->get_js_config()->get_child_int("nb_banks");
    int bank_width = this->get_js_config()->get_child_int("bank_width");

    this->bank_width         = bank_width;
    this->id_shift           = log2(bank_width);
    this->id_mask            = (1 << (int)log2(nb_banks)) - 1;
    this->offset_right_shift = log2(bank_width) + log2(nb_banks);
    this->offset_left_shift  = log2(bank_width);

    this->output_ports.resize(nb_banks);
    for (int i = 0; i < nb_banks; i++)
        this->new_master_port("out_" + std::to_string(i), &this->output_ports[i]);

    this->input_port.set_req_meth(&HWPEInterleaver::req);
    this->new_slave_port("input", &this->input_port);
}

vp::IoReqStatus HWPEInterleaver::req(vp::Block *__this, vp::IoReq *req)
{
    HWPEInterleaver *_this = (HWPEInterleaver *)__this;
    uint64_t offset  = req->get_addr();
    bool is_write    = req->get_is_write();
    uint64_t size    = req->get_size();
    uint8_t *data    = req->get_data();
    int max_latency  = 0;

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n",
        offset, size, is_write);

    /* Split the request across banks */
    while (size) {
        int bank_size = std::min(
            _this->bank_width - (offset & (_this->bank_width - 1)),
            size);

        int bank_id = (offset >> _this->id_shift) & _this->id_mask;
        uint64_t bank_offset =
            ((offset >> _this->offset_right_shift) << _this->offset_left_shift) +
            (offset & ((1 << _this->offset_left_shift) - 1));

        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "  → Bank %d, size %d\n", bank_id, bank_size);

        vp::IoReq *bank_req = new vp::IoReq;
        bank_req->init();
        bank_req->set_addr(bank_offset);
        bank_req->set_size(bank_size);
        bank_req->set_data(data);
        bank_req->set_is_write(is_write);
        _this->output_ports[bank_id].req_forward(bank_req);

        int latency = bank_req->get_latency();
        max_latency = latency > max_latency ? latency : max_latency;
        delete bank_req;

        offset += bank_size;
        size   -= bank_size;
        data   += bank_size;
    }

    if (max_latency > 0) {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Max bank latency: %d\n", max_latency);
    }
    req->inc_latency(max_latency);
    return vp::IoReqStatus::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new HWPEInterleaver(config);
}

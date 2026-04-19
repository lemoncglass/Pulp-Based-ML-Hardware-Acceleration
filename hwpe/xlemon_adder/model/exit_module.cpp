/*
 * Minimal exit module for GVSoC — write any value to trigger simulation quit.
 * Write 0 for success, non-zero for failure.
 */
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

class ExitModule : public vp::Component
{
public:
    ExitModule(vp::ComponentConf &config) : vp::Component(config)
    {
        this->in.set_req_meth(&ExitModule::req);
        this->new_slave_port("input", &this->in, this);
    }

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req)
    {
        ExitModule *_this = (ExitModule *)__this;
        if (req->get_is_write())
        {
            uint32_t value = 0;
            memcpy(&value, req->get_data(), req->get_size() < 4 ? req->get_size() : 4);
            _this->time.get_engine()->quit(value & 0x7fffffff);
        }
        return vp::IO_REQ_OK;
    }

private:
    vp::IoSlave in;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ExitModule(config);
}

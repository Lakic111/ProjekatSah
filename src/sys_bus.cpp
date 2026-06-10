#include "sys_bus.hpp"

using namespace sc_core;
using namespace tlm;

sys_bus::sys_bus(sc_module_name name) : sc_module(name), s_cpu("s_cpu"),
    i_bram("i_bram"), i_ncc("i_ncc"), i_dma("i_dma") {
    s_cpu.register_b_transport(this, &sys_bus::b_transport);
}

void sys_bus::b_transport(tlm_generic_payload& trans, sc_time& offset) {
    uint64_t addr = trans.get_address();

    // Rutiranje ka PL
    if (addr >= ADDR_DMA) {
        trans.set_address(addr - ADDR_DMA);
        i_dma->b_transport(trans, offset);
        trans.set_address(addr);
    }
    else if (addr >= ADDR_NCC) {
        trans.set_address(addr - ADDR_NCC);
        i_ncc->b_transport(trans, offset);
        trans.set_address(addr);
    }
    else if (addr >= ADDR_BRAM) {
        trans.set_address(addr - ADDR_BRAM);
        i_bram->b_transport(trans, offset);
        trans.set_address(addr);
    }
    else {
        trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
    }
}

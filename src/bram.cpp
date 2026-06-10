#include "bram.hpp"
#include <cstring>

using namespace sc_core;
using namespace tlm;

BRAM_Module::BRAM_Module(sc_module_name name) : sc_module(name), socket("socket"), mem(1024 * 1024, 0) {
    socket.register_b_transport(this, &BRAM_Module::b_transport);
}

void BRAM_Module::b_transport(int /*id*/, tlm_generic_payload& trans, sc_time& delay) {
    tlm_command cmd = trans.get_command();
    uint64_t    addr = trans.get_address();
    unsigned char* ptr = trans.get_data_ptr();
    unsigned int len = trans.get_data_length();

    // bounds
    if (addr + len > mem.size()) {
        trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    // command
    switch (cmd) {
        case TLM_READ_COMMAND:
            memcpy(ptr, &mem[addr], len);
            break;
        case TLM_WRITE_COMMAND:
            memcpy(&mem[addr], ptr, len);
            break;
        case TLM_IGNORE_COMMAND:
            break;
        default:
            trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
            return;
    }

    // PL prenosi podataka (BRAM pristup) se preklapaju sa obradom i nisu na
    // kritičnoj putanji - na njoj su samo setup (DMA) i obrada (NCC). Zato se
    // vreme BRAM pristupa ne uračunava (delay ostaje nepromenjen).
    trans.set_response_status(TLM_OK_RESPONSE);
}

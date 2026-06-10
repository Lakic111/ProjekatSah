#include "vp.hpp"

using namespace sc_core;
using namespace tlm;

vp::vp(sc_module_name name) : sc_module(name), s_cpu("s_cpu"), s_bus_int("s_bus_int"), i_ddr("i_ddr") {
    bus  = new sys_bus("bus");
    ncc  = new NCC_Target("ncc");
    bram = new BRAM_Module("bram");
    ddr  = new DDR_Module("ddr");
    dma  = new DMA_Module("dma");

    
    //DDR opseg -> i_ddr, PL opseg -> sys_bus
    s_cpu.register_b_transport(this, &vp::b_transport_bus);
    s_bus_int.bind(bus->s_cpu);
    i_ddr.bind(ddr->socket);            // CPU -> DDR

    // CPU konfiguriše PL periferije preko interkonekta 
    bus->i_ncc.bind(ncc->socket);
    bus->i_bram.bind(bram->socket);
    bus->i_dma.bind(dma->s_cpu);

    // DMA master: čita DDR, piše BRAM 
    dma->i_rd.bind(ddr->socket);        // DMA -> DDR
    dma->i_wr.bind(bram->socket);       // DMA -> BRAM

    // NCC master:čita sliku i šablon iz BRAM-a
    ncc->i_bram.bind(bram->socket);     // NCC -> BRAM
}

vp::~vp() {
    delete bus;
    delete ncc;
    delete bram;
    delete ddr;
    delete dma;
}

// CPU putanja: DDR ili PL periferije 
void vp::b_transport_bus(tlm_generic_payload& trans, sc_time& delay) {
    uint64_t addr = trans.get_address();
    if (addr < ADDR_BRAM) {
        i_ddr->b_transport(trans, delay);     // PS memorija (DDR)
    } else {
        s_bus_int->b_transport(trans, delay); // PL periferije (BRAM / NCC / DMA registri)
    }
}

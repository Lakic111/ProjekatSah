#include "dma.hpp"
#include <vector>

using namespace sc_core;
using namespace tlm;
using namespace std;

// ============================================================================
//  PROCESNI MODEL:
//  - b_transport na START registar SAMO okine start_ev i odmah se vrati.
//  - dma_proc() je SC_THREAD: izvrši prenos funkcionalno, a kroz wait() pusti
//    da prođe SAMO setup vreme (pokretanje FSM-a + AXI komanda). Vreme samog
//    prenosa bajtova se ZANEMARUJE jer se preklapa sa korisnim poslom (NCC).
//  - DDR latencija se ne računa (DDR je u bržem PS clock domenu).
// ============================================================================

DMA_Module::DMA_Module(sc_module_name name) : sc_module(name), s_cpu("s_cpu"), i_rd("i_rd"), i_wr("i_wr"),
                                              src(0), dst(0), w(0), h(0), src_stride(0), dst_stride(0) {
    s_cpu.register_b_transport(this, &DMA_Module::b_transport);
    SC_THREAD(dma_proc);
}

void DMA_Module::b_transport(tlm_generic_payload& trans, sc_time& delay) {
    if (trans.get_command() == TLM_WRITE_COMMAND) {
        uint64_t addr = trans.get_address();
        uint64_t val = (trans.get_data_length() == 8) ? *(uint64_t*)trans.get_data_ptr() : *(uint32_t*)trans.get_data_ptr();

        if (addr == 0x00) src = val;
        else if (addr == 0x08) dst = val;
        else if (addr == 0x10) w = val;
        else if (addr == 0x14) h = val;
        else if (addr == 0x18) src_stride = val;
        else if (addr == 0x1C) dst_stride = val;
        else if (addr == 0x20 && val == 1) {
            // Samo okidanje - prenos i njegovo (zanemareno) vreme idu u dma_proc().
            start_ev.notify(SC_ZERO_TIME);
        }
    }
    trans.set_response_status(TLM_OK_RESPONSE);
}

// SC_THREAD: radi paralelno sa CPU-om i NCC-om.
void DMA_Module::dma_proc() {
    while (true) {
        wait(start_ev);

        do_transfer();                 // funkcionalno kopiranje (bez naplate vremena)

        // Naplaćuje se SAMO setup; prenos se preklapa sa korisnim poslom.
        wait(20, SC_NS);               // 2 ciklusa: validacija + prihvat
        wait(250, SC_NS);              // 25 ciklusa: pokretanje interne FSM
        wait(50, SC_NS);               // 5 ciklusa: slanje AXI Read komande

        done_ev.notify();
    }
}

// DDR (src) -> BRAM (dst). Vreme prenosa se ODBACUJE (lokalni scratch delay),
// jer se po uputstvu zanemaruje. Funkcionalno kopiranje ostaje tačno.
void DMA_Module::do_transfer() {
    sc_time scratch = SC_ZERO_TIME;
    uint64_t src_local = src - ADDR_DDR;
    uint64_t dst_local = dst - ADDR_BRAM;

    if (src_stride == w && dst_stride == w) {
        uint32_t total_len = w * h;
        vector<uint8_t> buffer(total_len);

        tlm_generic_payload pl_rd;
        pl_rd.set_command(TLM_READ_COMMAND);
        pl_rd.set_address(src_local);
        pl_rd.set_data_ptr(buffer.data());
        pl_rd.set_data_length(total_len);
        i_rd->b_transport(pl_rd, scratch);

        tlm_generic_payload pl_wr;
        pl_wr.set_command(TLM_WRITE_COMMAND);
        pl_wr.set_address(dst_local);
        pl_wr.set_data_ptr(buffer.data());
        pl_wr.set_data_length(total_len);
        i_wr->b_transport(pl_wr, scratch);
    } else {
        vector<uint8_t> buffer(w);
        for (uint32_t y = 0; y < h; y++) {
            tlm_generic_payload pl_rd;
            pl_rd.set_command(TLM_READ_COMMAND);
            pl_rd.set_address(src_local + y * src_stride);
            pl_rd.set_data_ptr(buffer.data());
            pl_rd.set_data_length(w);
            i_rd->b_transport(pl_rd, scratch);

            tlm_generic_payload pl_wr;
            pl_wr.set_command(TLM_WRITE_COMMAND);
            pl_wr.set_address(dst_local + y * dst_stride);
            pl_wr.set_data_ptr(buffer.data());
            pl_wr.set_data_length(w);
            i_wr->b_transport(pl_wr, scratch);
        }
    }
}

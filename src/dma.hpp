#ifndef DMA_HPP
#define DMA_HPP

#include "common.hpp"

class DMA_Module : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(DMA_Module);

    tlm_utils::simple_target_socket<DMA_Module>    s_cpu; // kontrolni registri (CPU preko sys_bus)
    tlm_utils::simple_initiator_socket<DMA_Module> i_rd;  // čita iz DDR-a (PS memorija)
    tlm_utils::simple_initiator_socket<DMA_Module> i_wr;  // piše u BRAM (PL bafer)

    // Procesni model: CPU upisom START (0x20=1) okine start_ev; prenos teče u
    // sopstvenom SC_THREAD-u paralelno, a po završetku se javi done_ev.
    sc_core::sc_event start_ev;
    sc_core::sc_event done_ev;

    uint64_t src, dst;
    uint32_t w, h, src_stride, dst_stride;

    DMA_Module(sc_core::sc_module_name name);
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
    void dma_proc();        // SC_THREAD: setup vreme preko wait(), prenos "besplatan"
    void do_transfer();     // funkcionalno kopiranje (vreme se zanemaruje)
};

#endif // DMA_HPP

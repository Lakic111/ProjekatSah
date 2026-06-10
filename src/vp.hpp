#ifndef VP_HPP
#define VP_HPP

#include "common.hpp"
#include "sys_bus.hpp"
#include "bram.hpp"
#include "ddr.hpp"
#include "dma.hpp"
#include "ncc.hpp"

class vp : public sc_core::sc_module {
public:
    // Jedini ulaz: CPU (PS). Ovde vp modeluje PS adresni dekoder:
    // DDR opseg -> DDR kontroler, PL opseg -> GP port (sys_bus).
    tlm_utils::simple_target_socket<vp> s_cpu;

    tlm_utils::simple_initiator_socket<vp> s_bus_int; // ka PL interkonektu (sys_bus)
    tlm_utils::simple_initiator_socket<vp> i_ddr;     // CPU -> DDR (PS memorija)

    vp(sc_core::sc_module_name name);
    ~vp();

    // "Interrupt" linije ka CPU-u: tb čeka na ove događaje (wait na završetak).
    sc_core::sc_event& nccDone() { return ncc->done_ev; }
    sc_core::sc_event& dmaDone() { return dma->done_ev; }

private:
    sys_bus* bus;
    NCC_Target* ncc;
    BRAM_Module* bram;
    DDR_Module* ddr;
    DMA_Module* dma;

    void b_transport_bus(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
};

#endif

#ifndef BRAM_HPP
#define BRAM_HPP

#include "common.hpp"
#include <vector>

class BRAM_Module : public sc_core::sc_module {
public:
    // BRAM je deljeni PL bafer. Tri mastera mu pristupaju:
    //   - CPU  (upis šablona, čitanje segmenta za proveru)
    //   - DMA  (upis isečenog segmenta)
    //   - NCC  (čitanje slike i šablona)
    // multi_passthrough dozvoljava da mu pristupaju sva tri mastera
    tlm_utils::multi_passthrough_target_socket<BRAM_Module> socket;
    std::vector<uint8_t> mem;

    BRAM_Module(sc_core::sc_module_name name);
    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
};

#endif // BRAM_HPP

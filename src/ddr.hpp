#ifndef DDR_HPP
#define DDR_HPP

#include "common.hpp"
#include <vector>

class DDR_Module : public sc_core::sc_module {
public:
    // DDR je PS memorija. Dva mastera mu pristupaju:
    //   - CPU (učitavanje slike na početku)
    //   - DMA (čitanje segmenata, modeluje AXI HP put)
    // Zato koristimo multi_passthrough
    tlm_utils::multi_passthrough_target_socket<DDR_Module> socket;
    std::vector<uint8_t> mem;

    DDR_Module(sc_core::sc_module_name name);
    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
};

#endif // DDR_HPP

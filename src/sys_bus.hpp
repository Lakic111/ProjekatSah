#ifndef SYS_BUS_HPP
#define SYS_BUS_HPP

#include "common.hpp"

class sys_bus : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<sys_bus> s_cpu; 

    tlm_utils::simple_initiator_socket<sys_bus> i_bram;
    tlm_utils::simple_initiator_socket<sys_bus> i_ncc;
    tlm_utils::simple_initiator_socket<sys_bus> i_dma;

    sys_bus(sc_core::sc_module_name name);
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& offset);
};

#endif

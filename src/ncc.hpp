#ifndef NCC_HPP
#define NCC_HPP

#include "common.hpp"
#include <vector>

class NCC_Target : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(NCC_Target);

    tlm_utils::simple_target_socket<NCC_Target>    socket; // kontrolni registri (CPU preko sys_bus)
    tlm_utils::simple_initiator_socket<NCC_Target> i_bram; // NCC sam čita sliku i šablon iz BRAM-a

    // Procesni model: CPU okine start_ev (upisom CTRL=1), NCC radi u sopstvenom
    // SC_THREAD-u i po završetku javi done_ev. Tako obrada teče paralelno sa
    // ostatkom sistema, umesto da se njeno vreme sabira u b_transport-u.
    sc_core::sc_event start_ev;
    sc_core::sc_event done_ev;   // "interrupt": CPU ga čeka preko vp accessor-a

    std::vector<uint8_t> image, templ;
    std::vector<int32_t> result_map;
    int img_w, img_h, tmp_w, tmp_h;
    uint64_t img_addr, tmp_addr;
    int      template_mean;
    uint32_t hw_status;

    NCC_Target(sc_core::sc_module_name name);
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
    void ncc_proc();                 // SC_THREAD: troši vreme obrade preko wait()
    void read_from_bram(uint64_t bram_addr, unsigned char* dst, unsigned int len);
    void calculate_template_mean();
    void compute_full_matrix();
    result_t solve_single_point(int u, int v);
};

#endif // NCC_HPP

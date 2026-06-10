#include "ncc.hpp"
#include <cstring>

using namespace sc_core;
using namespace sc_dt;
using namespace tlm;
using namespace std;

// ============================================================================
//  PROCESNI MODEL (paralelnost):
//  - b_transport na CTRL=1 SAMO okine start_ev i odmah se vrati (ne računa
//    vreme obrade), pa CPU nije blokiran sekvencijalno.
//  - ncc_proc() je SC_THREAD: čeka start_ev, izračuna rezultat i kroz wait()
//    pusti da prođe modelovano vreme obrade, zatim javi done_ev.
//  - Učitavanje slike/šablona iz BRAM-a se modeluje funkcionalno, ali se
//    njegovo vreme ZANEMARUJE (prenos se preklapa sa korisnim poslom).
// ============================================================================

NCC_Target::NCC_Target(sc_module_name name) : sc_module(name), socket("socket"), i_bram("i_bram"),
    img_w(0), img_h(0), tmp_w(0), tmp_h(0), img_addr(0), tmp_addr(0), template_mean(0), hw_status(0) {
    socket.register_b_transport(this, &NCC_Target::b_transport);
    SC_THREAD(ncc_proc);
}

// NCC kao master čita iz BRAM-a. Vreme ovog prenosa se NE uračunava
// (odbacujemo lokalni delay), jer se dešava uporedo sa korisnim poslom.
void NCC_Target::read_from_bram(uint64_t bram_addr, unsigned char* dst, unsigned int len) {
    sc_time scratch = SC_ZERO_TIME;
    tlm_generic_payload pl;
    pl.set_command(TLM_READ_COMMAND);
    pl.set_address(bram_addr - ADDR_BRAM);
    pl.set_data_ptr(dst);
    pl.set_data_length(len);
    i_bram->b_transport(pl, scratch);
}

void NCC_Target::b_transport(tlm_generic_payload& trans, sc_time& delay) {
    tlm_command cmd = trans.get_command();
    uint64_t    addr = trans.get_address();
    unsigned char* ptr = trans.get_data_ptr();

    if (cmd == TLM_WRITE_COMMAND) {
        if (addr == REG_IMG_W) img_w = *(int*)ptr;
        else if (addr == REG_IMG_H) img_h = *(int*)ptr;
        else if (addr == REG_TMP_W) tmp_w = *(int*)ptr;
        else if (addr == REG_TMP_H) tmp_h = *(int*)ptr;
        else if (addr == REG_IMG_ADDR) {
            img_addr = *(uint32_t*)ptr;
            image.resize((size_t)img_w * img_h);
            read_from_bram(img_addr, image.data(), (unsigned int)(img_w * img_h));
        }
        else if (addr == REG_TMP_ADDR) {
            tmp_addr = *(uint32_t*)ptr;
            templ.resize((size_t)tmp_w * tmp_h);
            read_from_bram(tmp_addr, templ.data(), (unsigned int)(tmp_w * tmp_h));
            calculate_template_mean();
        }
        else if (addr == REG_CTRL && *(uint32_t*)ptr == 1) {
            // Samo okidanje procesa - vreme obrade prolazi u ncc_proc(), ne ovde.
            hw_status = 0;          // BUSY
            start_ev.notify(SC_ZERO_TIME);
        }
        trans.set_response_status(TLM_OK_RESPONSE);
    }
    else if (cmd == TLM_READ_COMMAND) {
        unsigned int len = trans.get_data_length();
        if (addr == REG_STATUS) {
            memcpy(ptr, &hw_status, sizeof(uint32_t));
        }
        else if (addr == ADDR_RESULTS) {
            memcpy(ptr, result_map.data(), len);
        }
        trans.set_response_status(TLM_OK_RESPONSE);
    }
}

// SC_THREAD: hardverski blok koji radi paralelno sa CPU-om i DMA-om.
void NCC_Target::ncc_proc() {
    // broj ciklusa iz Vitis HLS izveštaja -> modelovano vreme obrade
    const long long hls_ciklusi = 10360183;
    while (true) {
        wait(start_ev);
        hw_status = 0;                                  // BUSY
        compute_full_matrix();                          // funkcionalni rezultat
        wait(hls_ciklusi * 10, SC_NS);                  // OVDE prolazi vreme obrade
        hw_status = 1;                                  // DONE
        done_ev.notify();                               // "interrupt" ka CPU
    }
}

void NCC_Target::calculate_template_mean() {
    if (templ.empty()) { template_mean = 0; return; }
    sc_uint<22> sum = 0;
    for (uint8_t val : templ) sum += (sc_uint<8>)val;
    unsigned n = (unsigned)templ.size();
    template_mean = (int)((sum.to_uint() + (n >> 1)) / n);
}

void NCC_Target::compute_full_matrix() {
    int res_w = img_w - tmp_w + 1;
    int res_h = img_h - tmp_h + 1;
    if (res_w <= 0 || res_h <= 0) return;

    result_map.assign(res_w * res_h, 0);
    for (int v = 0; v < res_h; v++) {
        for (int u = 0; u < res_w; u++) {
            result_t val = solve_single_point(u, v);
            result_map[v * res_w + u] = static_cast<int32_t>(val);
        }
    }
}

result_t NCC_Target::solve_single_point(int u, int v) {
    const int count = tmp_w * tmp_h;
    if (count == 0) return 0;

    sc_uint<22> sum_f = 0;
    for (int y = 0; y < tmp_h; y++)
        for (int x = 0; x < tmp_w; x++)
            sum_f += (sc_uint<8>) image[(v + y) * img_w + (u + x)];

    sc_uint<8> f_bar = (sc_uint<8>)((sum_f.to_uint() + (count >> 1)) / count);

    sc_int<30>  sum_num   = 0;
    sc_uint<29> sum_den_f = 0;
    sc_uint<29> sum_den_t = 0;

    for (int y = 0; y < tmp_h; y++) {
        for (int x = 0; x < tmp_w; x++) {
            sc_int<9> diff_f = (int)image[(v + y) * img_w + (u + x)] - (int)f_bar;
            sc_int<9> diff_t = (int)templ [y * tmp_w + x]            - template_mean;

            sc_int<17> p_num = diff_f * diff_t;
            sc_int<17> p_ff  = diff_f * diff_f;
            sc_int<17> p_tt  = diff_t * diff_t;

            sum_num   += p_num;
            sum_den_f += (sc_uint<17>) p_ff;
            sum_den_t += (sc_uint<17>) p_tt;
        }
    }

    if (sum_den_f == 0 || sum_den_t == 0) return 0;

    sc_uint<58> num_sq   = (sc_uint<58>)(sum_num.to_int64()  * sum_num.to_int64());
    sc_uint<58> den_prod = (sc_uint<58>)(sum_den_f.to_uint64() * sum_den_t.to_uint64());
    if (den_prod == 0) return 0;

    // NCC^2 kao sc_ufixed<32,1> (Q1.31). Deljenje u širem ufixed tipu da
    // bi se zadržali frakcioni bitovi.
    sc_ufixed<90, 58> num_f   = num_sq.to_double();
    sc_ufixed<90, 58> den_f2  = den_prod.to_double();
    sc_ufixed<32, 1, SC_RND, SC_SAT> ncc2 = num_f / den_f2;

    return (result_t) ncc2.range(31, 0).to_uint();
}

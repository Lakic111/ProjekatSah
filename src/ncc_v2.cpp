#include "ncc.hpp"
#include <cstring>

// ============================================================================
//  ncc_v2.cpp - alternativna verzija NCC implementacije
//
//  Razlike u odnosu na ncc.cpp (trenutna verzija):
//    - solve_single_point() vraca se kao posebna funkcija (original logika)
//    - compute_full_matrix() poziva solve_single_point za svaki piksel (original)
//    - Latencija je FIKSNA: 10360183 ciklusa * 10 ns (ne skalira po dimenzijama)
//
//  Zajednicki delovi sa ncc.cpp (zadrzano):
//    - Nema sc_uint / sc_int / sc_ufixed — zamenjeno sa int / int64_t / double
//    - BRAM citanje se odvija u ncc_proc() SC_THREAD-u, ne blokira CPU
//
//  Da bi se koristio umesto ncc.cpp:
//    1. u ncc.hpp dodati u private sekciju: result_t solve_single_point(int u, int v);
//    2. u Makefile zameniti ncc.cpp sa ncc_v2.cpp
// ============================================================================

using namespace sc_core;
using namespace tlm;
using namespace std;

NCC_Target::NCC_Target(sc_module_name name) : sc_module(name), socket("socket"), i_bram("i_bram"),
    img_w(0), img_h(0), tmp_w(0), tmp_h(0), img_addr(0), tmp_addr(0), template_mean(0), hw_status(0) {
    socket.register_b_transport(this, &NCC_Target::b_transport);
    SC_THREAD(ncc_proc);
}

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
    tlm_command    cmd  = trans.get_command();
    uint64_t       addr = trans.get_address();
    unsigned char* ptr  = trans.get_data_ptr();

    if (cmd == TLM_WRITE_COMMAND) {
        if      (addr == REG_IMG_W)    img_w    = *(int*)ptr;
        else if (addr == REG_IMG_H)    img_h    = *(int*)ptr;
        else if (addr == REG_TMP_W)    tmp_w    = *(int*)ptr;
        else if (addr == REG_TMP_H)    tmp_h    = *(int*)ptr;
        else if (addr == REG_IMG_ADDR) img_addr = *(uint32_t*)ptr;
        else if (addr == REG_TMP_ADDR) tmp_addr = *(uint32_t*)ptr;
        else if (addr == REG_CTRL && *(uint32_t*)ptr == 1) {
            hw_status = 0;
            start_ev.notify(SC_ZERO_TIME);
        }
        trans.set_response_status(TLM_OK_RESPONSE);
    }
    else if (cmd == TLM_READ_COMMAND) {
        unsigned int len = trans.get_data_length();
        if (addr == REG_STATUS)
            memcpy(ptr, &hw_status, sizeof(uint32_t));
        else if (addr == ADDR_RESULTS)
            memcpy(ptr, result_map.data(), len);
        trans.set_response_status(TLM_OK_RESPONSE);
    }
}

void NCC_Target::ncc_proc() {
    const long long hls_ciklusi = 10360183LL;
    while (true) {
        wait(start_ev);
        hw_status = 0;

        image.resize((size_t)img_w * img_h);
        read_from_bram(img_addr, image.data(), (unsigned int)(img_w * img_h));
        templ.resize((size_t)tmp_w * tmp_h);
        read_from_bram(tmp_addr, templ.data(), (unsigned int)(tmp_w * tmp_h));
        calculate_template_mean();

        compute_full_matrix();

        wait(hls_ciklusi * 10, SC_NS);

        hw_status = 1;
        done_ev.notify();
    }
}

void NCC_Target::calculate_template_mean() {
    if (templ.empty()) { template_mean = 0; return; }
    int sum = 0;
    for (uint8_t val : templ) sum += val;
    int n = (int)templ.size();
    template_mean = (sum + (n >> 1)) / n;
}

void NCC_Target::compute_full_matrix() {
    int res_w = img_w - tmp_w + 1;
    int res_h = img_h - tmp_h + 1;
    if (res_w <= 0 || res_h <= 0) return;

    result_map.assign(res_w * res_h, 0);
    for (int v = 0; v < res_h; v++) {
        for (int u = 0; u < res_w; u++) {
            result_map[v * res_w + u] = (int32_t)solve_single_point(u, v);
        }
    }
}

result_t NCC_Target::solve_single_point(int u, int v) {
    const int count = tmp_w * tmp_h;
    if (count == 0) return 0;

    // f_bar: srednja vrednost prozora slike na poziciji (u, v)
    int sum_f = 0;
    for (int y = 0; y < tmp_h; y++)
        for (int x = 0; x < tmp_w; x++)
            sum_f += image[(v + y) * img_w + (u + x)];
    int f_bar = (sum_f + (count >> 1)) / count;

    // Sume za NCC formulu
    int64_t sum_num   = 0;
    int64_t sum_den_f = 0;
    int64_t sum_den_t = 0;

    for (int y = 0; y < tmp_h; y++) {
        for (int x = 0; x < tmp_w; x++) {
            int diff_f = (int)image[(v + y) * img_w + (u + x)] - f_bar;
            int diff_t = (int)templ [y * tmp_w + x]             - template_mean;

            sum_num   += (int64_t)diff_f * diff_t;
            sum_den_f += (int64_t)diff_f * diff_f;
            sum_den_t += (int64_t)diff_t * diff_t;
        }
    }

    if (sum_den_f == 0 || sum_den_t == 0) return 0;

    uint64_t num_sq   = (uint64_t)(sum_num * sum_num);
    uint64_t den_prod = (uint64_t)sum_den_f * (uint64_t)sum_den_t;
    if (den_prod == 0) return 0;

    // NCC^2 u Q1.31: vrednost 2147483648 odgovara NCC^2 = 1.0
    double ncc2 = (double)num_sq / (double)den_prod;
    return (result_t)(ncc2 * 2147483648.0);
}

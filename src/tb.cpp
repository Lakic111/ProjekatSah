#include "tb.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

using namespace sc_core;
using namespace tlm;
using namespace std;

tb_vp::tb_vp(sc_module_name name) : sc_module(name), isoc("isoc") {
    SC_THREAD(test);
}

void tb_vp::loadFile(string path, vector<uint8_t>& data, int& w, int& h) {
    ifstream f(path);
    if(!f.is_open()) { cout << "[WARNING] Missing: " << path << endl; return; }
    string line; h = 0; w = 0;
    while (getline(f, line)) {
        stringstream ss(line); string val; int row_w = 0;
        while (getline(ss, val, ',')) {
            data.push_back(static_cast<uint8_t>(stod(val)));
            row_w++;
        }
        if (w == 0) w = row_w; h++;
    }
}

void tb_vp::test() {
    vector<uint8_t> img_raw;
    int iw = 720, ih = 720;
    sc_time delay = SC_ZERO_TIME;

    // FEN Mapping and Board State Array
    char fen_map[12] = {'Q', 'N', 'K', 'B', 'P', 'R', 'q', 'n', 'k', 'b', 'p', 'r'};
    char board_state[8][8];
    for(int i=0; i<8; i++) for(int j=0; j<8; j++) board_state[i][j] = ' ';

    // Slika cele table -> DDR (PS memorija)
    loadFile("data/board2.txt", img_raw, iw, ih);
    send_data(ADDR_DDR, img_raw, delay);

    const string template_names[12] = {
        "data/Belakraljicatemplate.txt", "data/Belikonjtemplate.txt", "data/Belikraljtemplate.txt",
        "data/Belilovactemplate.txt", "data/Belipesaktemplate.txt", "data/Belitoptemplate.txt",
        "data/Crnakraljicatemplate.txt", "data/Crnikonjtemplate.txt", "data/Crnikraljtemplate.txt",
        "data/Crnilovactemplate.txt", "data/Crnipesaktemplate.txt", "data/Crnitoptemplate.txt"
    };

    // Šabloni -> BRAM (CPU ih upisuje preko sys_bus)
    vector<vector<uint8_t>> templates(12);
    int t_w[12], t_h[12];
    uint64_t current_bram_offset = 0;
    uint64_t template_bram_addrs[12];

    for (int i = 0; i < 12; i++) {
        loadFile(template_names[i], templates[i], t_w[i], t_h[i]);
        template_bram_addrs[i] = ADDR_BRAM + current_bram_offset;
        if (!templates[i].empty()) {
            send_data(template_bram_addrs[i], templates[i], delay);
            current_bram_offset += templates[i].size();
        }
    }

    int seg_w = 90, seg_h = 90;
    write_reg32(ADDR_NCC + REG_IMG_W, seg_w, delay);
    write_reg32(ADDR_NCC + REG_IMG_H, seg_h, delay);

    int executed_count = 0;

    for (int m = 0; m < 8; m++) {
        for (int n = 0; n < 8; n++) {

            uint64_t ddr_seg_addr  = ADDR_DDR + (m * seg_h * iw) + (n * seg_w);

            // Provjera praznog polja direktno iz DDR-a (bez DMA-a).
            // Čitamo centralnu zonu segmenta red po red (stride-aware).
            int sum = 0, count = 0;
            for (int y = 29; y <= 59; y++) {
                vector<uint8_t> row_buf(31);
                read_data(ADDR_DDR + ((m * seg_h + y) * iw) + (n * seg_w + 29), row_buf, delay);
                for (uint8_t v : row_buf) { sum += v; count++; }
            }
            int mean = sum / count;

            uint8_t p10_val;
            read_data(ADDR_DDR + ((m * seg_h + 10) * iw) + (n * seg_w + 10), &p10_val, 1, delay);

            if (mean != (int)p10_val) {
                uint64_t bram_seg_addr = ADDR_BRAM + 0x80000;

                // DMA samo za neprazna polja
                configure_dma(ddr_seg_addr, bram_seg_addr, seg_w, seg_h, iw, seg_w, delay);
                wait(*m_dma_done);

                cout << "\n[TB] Processing Segment (" << m << "," << n << ") - Evaluating against 12 templates..." << endl;

                // Kažemo NCC-u gde je segment slike u BRAM-u -> NCC ga sam pročita
                write_reg32(ADDR_NCC + REG_IMG_ADDR, (uint32_t)bram_seg_addr, delay);

                // NCC vraća fixed-point ceo broj (NCC²·2^31, Q1.31). Za izbor najboljeg
                // šablona (argmax) decimalna vrednost NIJE potrebna - poređenje
                // "koji je veći" daje isti rezultat na sirovim celim brojevima,
                // pa tb ne radi nikakvu konverziju ni deljenje.
                int32_t absolute_max = -1;
                int best_template_idx = -1;

                for (int t = 0; t < 12; t++) {
                    if (templates[t].empty()) continue;

                    write_reg32(ADDR_NCC + REG_TMP_W, t_w[t], delay);
                    write_reg32(ADDR_NCC + REG_TMP_H, t_h[t], delay);

                    // Kažemo NCC-u gde je šablon u BRAM-u -> NCC ga sam pročita
                    write_reg32(ADDR_NCC + REG_TMP_ADDR, (uint32_t)template_bram_addrs[t], delay);

                    write_reg32(ADDR_NCC + REG_CTRL, 1, delay);

                    // NCC radi u svom procesu paralelno; čekamo "done" događaj
                    // umesto pollinga (ovde prolazi vreme obrade NCC-a).
                    wait(*m_ncc_done);

                    int rw = seg_w - t_w[t] + 1;
                    int rh = seg_h - t_h[t] + 1;
                    int res_points = rw * rh;

                    vector<int32_t> res_raw(res_points);
                    read_data(ADDR_NCC + ADDR_RESULTS, (uint8_t*)res_raw.data(), res_raw.size() * sizeof(int32_t), delay);

                    // Maksimum direktno nad sirovim fixed-point vrednostima
                    int32_t max_v = -1;
                    for (int v = 0; v < rh; v++)
                        for (int u = 0; u < rw; u++)
                            if (res_raw[v * rw + u] > max_v) max_v = res_raw[v * rw + u];

                    if (max_v > absolute_max) {
                        absolute_max = max_v;
                        best_template_idx = t;
                    }
                }
                // Ispis: sirov fixed-point skor pretvoren u decimalni NCC^2.
                // (double) cast MORA biti pre deljenja, inace celobrojno deljenje daje 0.
                double best_coef2 = (double)absolute_max / 2147483648.0;   // /2^31 -> NCC^2 u [0,1]
                cout << ">>> BEST PIECE DETECTED: " << template_names[best_template_idx]
                     << " | NCC^2 = " << best_coef2
                     << " (raw " << absolute_max << ") <<<" << endl;

                if (best_template_idx != -1) {
                    board_state[m][n] = fen_map[best_template_idx];
                }
                executed_count++;
            }
        }
    }

    // --- FEN Generation Logic ---
    stringstream fen;
    for (int m = 0; m < 8; m++) {
        int empty_count = 0;
        for (int n = 0; n < 8; n++) {
            if (board_state[m][n] == ' ') {
                empty_count++;
            } else {
                if (empty_count > 0) {
                    fen << empty_count;
                    empty_count = 0;
                }
                fen << board_state[m][n];
            }
        }
        if (empty_count > 0) fen << empty_count;
        if (m < 7) fen << "/";
    }

    wait(delay);
    cout << "\n[TB] Completed. Evaluated " << executed_count << " populated squares." << endl;

    cout << "\n" << string(50, '=') << endl;
    cout << "FINAL FEN NOTATION:" << endl;
    cout << fen.str() << endl;
    cout << string(50, '=') << "\n" << endl;

    cout << "Total Sim Time: " << sc_time_stamp() << endl;
    sc_stop();
}

void tb_vp::write_reg32(uint64_t addr, uint32_t val, sc_time& d) {
    tlm_generic_payload pl;
    pl.set_command(TLM_WRITE_COMMAND);
    pl.set_address(addr);
    pl.set_data_ptr((unsigned char*)&val);
    pl.set_data_length(sizeof(uint32_t));
    isoc->b_transport(pl, d);
}
void tb_vp::write_reg64(uint64_t addr, uint64_t val, sc_time& d) {
    tlm_generic_payload pl;
    pl.set_command(TLM_WRITE_COMMAND);
    pl.set_address(addr);
    pl.set_data_ptr((unsigned char*)&val);
    pl.set_data_length(sizeof(uint64_t));
    isoc->b_transport(pl, d);
}
void tb_vp::send_data(uint64_t addr, vector<uint8_t>& data, sc_time& d) {
    tlm_generic_payload pl;
    pl.set_command(TLM_WRITE_COMMAND);
    pl.set_address(addr);
    pl.set_data_ptr(data.data());
    pl.set_data_length(data.size());
    isoc->b_transport(pl, d);
}
void tb_vp::read_data(uint64_t addr, vector<uint8_t>& data, sc_time& d) {
    read_data(addr, data.data(), data.size(), d);
}
void tb_vp::read_data(uint64_t addr, uint8_t* ptr, int len, sc_time& d) {
    tlm_generic_payload pl;
    pl.set_command(TLM_READ_COMMAND);
    pl.set_address(addr);
    pl.set_data_ptr(ptr);
    pl.set_data_length(len);
    isoc->b_transport(pl, d);
}
void tb_vp::configure_dma(uint64_t src, uint64_t dst, uint32_t w, uint32_t h, uint32_t s_stride, uint32_t d_stride, sc_time& d) {
    write_reg64(ADDR_DMA + 0x00, src, d);
    write_reg64(ADDR_DMA + 0x08, dst, d);
    write_reg32(ADDR_DMA + 0x10, w, d);
    write_reg32(ADDR_DMA + 0x14, h, d);
    write_reg32(ADDR_DMA + 0x18, s_stride, d);
    write_reg32(ADDR_DMA + 0x1C, d_stride, d);
    write_reg32(ADDR_DMA + 0x20, 1, d);
}

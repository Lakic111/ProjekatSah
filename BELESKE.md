# Beleske sa projekta chess-vp

## Sta je projekat
SystemC TLM-2.0 virtuelna platforma koja simulira detekciju sahovski figura
pomocu NCC (Normalized Cross-Correlation) na Zybo Z7-10 (Zynq-7010).

Ulaz: slika sahovske table 720x720 px
Izlaz: FEN notacija pozicije

---

## Arhitektura VP

```
CPU (tb_vp / PS)
  |
  |-- DDR (PS memorija, 0x00000000, 8 MB) -- slika table
  |
  +-- sys_bus (PL interkonekt)
       |-- BRAM   (0x40000000, 1 MB) -- sabloni + segment bafer (0x80000)
       |-- NCC0   (0x50000000)       -- NCC jedinica 0
       |-- NCC1   (0x51000000)       -- NCC jedinica 1  [DODATA]
       +-- DMA    (0x60000000)       -- kopira DDR segment -> BRAM
```

DMA ima dva inicijatora: `i_rd` (cita DDR) i `i_wr` (pise BRAM).
Oba NCC modula citaju iz BRAM-a putem svog `i_bram` socket-a.

---

## Registri NCC modula

| Offset | Naziv       | Opis                        |
|--------|-------------|-----------------------------|
| 0x00   | REG_IMG_W   | sirina slike (px)           |
| 0x04   | REG_IMG_H   | visina slike (px)           |
| 0x08   | REG_TMP_W   | sirina sablona (px)         |
| 0x0C   | REG_TMP_H   | visina sablona (px)         |
| 0x10   | REG_IMG_ADDR| adresa slike u BRAM-u       |
| 0x14   | REG_TMP_ADDR| adresa sablona u BRAM-u     |
| 0x30   | REG_CTRL    | pisanje 1 pokrace izracunavanje |
| 0x34   | REG_STATUS  | 1 = gotovo                  |
| 0x40   | ADDR_RESULTS| citanje result_map niza     |

---

## Kljucne izmene u ovoj sesiji

### 1. Uklonjen sc_fixed, sc_uint, sc_int
- Zamenjeni sa `double` / nativnim C++ tipovima (`int32_t`, `int64_t`, `uint8_t`)
- **Razlog**: sc_fixed/sc_uint su ekstremno spori u simulaciji
- Ubrzanje wall-clock vremena: otprilike 10-20x

### 2. Skaliranje latencije po dimenzijama sablona
Umesto fiksnih 10.360.183 ciklusa za svaki sablon, latencija se skalira:
```cpp
const long long ref_cycles = 10360183LL;
const double    ref_work   = 61.0 * 61.0 * 30.0 * 30.0;  // ref: 90x90 slika, 30x30 sablon
double actual_work = (double)res_w * res_h * tmp_w * tmp_h;
long long ciklusi  = (long long)(ref_cycles * actual_work / ref_work);
wait(ciklusi * 10, SC_NS);  // 10 ns = 1 ciklus na 100 MHz
```
- **Osnova**: Vitis HLS izvestaj za 90x90 sliku / 30x30 sablon dao 10.360.183 ciklusa

### 3. Integral image optimizacija (NCC wall-clock)
Umesto O(res_w * res_h * tmp_w * tmp_h) za racunanje f_bar:
- Izgradi summed area table (integral image) jednom: O(img_w * img_h)
- Svaki f_bar po prozoru: O(1) operacija (4 pristupa tabeli)
- Ukupno: O(img_w*img_h) + O(res_w*res_h*tmp_w*tmp_h) umesto O(res_w*res_h*tmp_w*tmp_h) sa skupim inner loop-om za sum

### 4. Dva paralelna NCC modula (NCC0 + NCC1)
Sabloni se obradjuju u **parovima**: NCC0 dobija sablon t, NCC1 dobija sablon t+1.
TB ceka oba: `wait(*m_ncc0_done & *m_ncc1_done)` (sc_event_and_list)
- 12 sablona / 2 = 6 iteracija umesto 12
- Smanjenje simulacionog vremena: ~35 s -> ~18.5 s (gotovo prepolovljeno)

---

## Bug koji je uzrokovao segfault (RESENO)

**Simptom**: `Segmentation fault` odmah posle "[TB] Processing Segment (0,0)"

**Uzrok**: NCC1 nikad nije dobijao `REG_IMG_W` i `REG_IMG_H`:
- `ncc1->img_w = 0`, `ncc1->img_h = 0`
- `image.resize(0)` -> prazan vektor
- `compute_full_matrix()` vraca se odmah jer `res_w < 0`
- `result_map` ostaje prazan vektor
- TB poziva `read_data(ADDR_NCC1 + ADDR_RESULTS, ptr, 14884)`:
  `memcpy(ptr, result_map.data(), 14884)` cita iz nullptr -> **segfault**

**Fix** (tb.cpp, inicijalizacija pre petlje):
```cpp
write_reg32(ADDR_NCC  + REG_IMG_W, seg_w);
write_reg32(ADDR_NCC  + REG_IMG_H, seg_h);
write_reg32(ADDR_NCC1 + REG_IMG_W, seg_w);  // DODATO
write_reg32(ADDR_NCC1 + REG_IMG_H, seg_h);  // DODATO
```

---

## Performanse (finalni rezultati)

| Metrika              | Vrednost        |
|----------------------|-----------------|
| Wall-clock vreme     | ~1.26 s         |
| Simulaciono vreme    | ~18.48 s        |
| Broj obradjenih polja| 32 (od 64)      |
| Prazna polja (preskocena) | 32 (is_square_empty optimizacija) |

Prethodno simulaciono vreme (bez dual NCC): ~35 s
Ubrzanje: ~1.9x (ocekivano za 2 paralelne jedinice)

---

## NCC izlazni format (Q1.31 fixed-point)

NCC^2 vrednost se cuva kao `uint32_t` u Q1.31:
```cpp
result_map[...] = (int32_t)((double)num_sq / (double)den_prod * 2147483648.0);
```
Citanje: `double best_coef2 = (double)absolute_max / 2147483648.0;`

NCC^2 (ne NCC) se cuva jer eliminise predznak (korelacija moze biti negativna).
Vrednost 1.0 = savrseno poklapanje.

---

## Zybo Z7-10 resursi

- 17.600 LUT-ova, 80 DSP-ova
- Dovoljan za 2 NCC jedinice (HLS izvestaj: 1 NCC ~ 6000 LUT-ova)
- Oba NCC modula dele isti BRAM port (cita su istovremena, nema konflikta)

---

## Fajlovi koji su izmenjeni

| Fajl            | Izmena                                               |
|-----------------|------------------------------------------------------|
| src/ncc.cpp     | Uklonjen sc_fixed; integral image; skaliranje lat.   |
| src/ncc.hpp     | Uklonjen solve_single_point; dodati novi vektori     |
| src/tb.cpp      | Dual NCC logika; is_square_empty; REG_IMG_W za oba   |
| src/tb.hpp      | connect_irq prima 3 eventa; m_ncc1_done              |
| src/sys_bus.cpp | Rutiranje za ADDR_NCC1 (pre ADDR_NCC provere)        |
| src/sys_bus.hpp | Dodat i_ncc1 initiator socket                        |
| src/vp.hpp      | ncc0/ncc1; ncc0Done()/ncc1Done()                     |
| src/vp.cpp      | Instanciranje ncc0 i ncc1; bind sockets              |
| src/sc_main.cpp | connect_irq sa &ncc0Done(), &ncc1Done(), &dmaDone()  |
| src/common.hpp  | ADDR_NCC1 = 0x51000000                               |

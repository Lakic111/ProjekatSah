#ifndef COMMON_HPP
#define COMMON_HPP

#define SC_INCLUDE_FX
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cstdint>

// PL Periferni adresni prostor
const uint64_t ADDR_BRAM = 0x40000000;
const uint64_t ADDR_NCC  = 0x50000000;
const uint64_t ADDR_DMA  = 0x60000000;

// PS Adresni prostor 
const uint64_t ADDR_DDR  = 0x00000000;

// NCC Registri
const uint64_t REG_IMG_W     = 0x00;
const uint64_t REG_IMG_H     = 0x04;
const uint64_t REG_TMP_W     = 0x08;
const uint64_t REG_TMP_H     = 0x0C;
const uint64_t REG_IMG_ADDR  = 0x10; // BRAM adresa segmenta slike (NCC ga sam čita)
const uint64_t REG_TMP_ADDR  = 0x14; // BRAM adresa šablona (NCC ga sam čita)
const uint64_t REG_CTRL      = 0x30;
const uint64_t REG_STATUS    = 0x34;
const uint64_t ADDR_RESULTS  = 0x40;

typedef uint8_t  pixel_t;
typedef double   mean_t;
typedef double   diff_t;
typedef double   accum_t;
typedef double   sq_accum_t;
typedef uint32_t result_t;

#endif

    #pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32_reg.h"

#define TYPE_ESP32_RSA "misc.esp32.rsa"
#define ESP32_RSA(obj) OBJECT_CHECK(Esp32RsaState, (obj), TYPE_ESP32_RSA)

#define ESP32_RSA_MEM_BLK_SIZE    0x200

typedef struct Esp32RsaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t rsa_m_mem[ESP32_RSA_MEM_BLK_SIZE / 4];
    uint32_t rsa_z_mem[ESP32_RSA_MEM_BLK_SIZE / 4];
    uint32_t rsa_y_mem[ESP32_RSA_MEM_BLK_SIZE / 4];
    uint32_t rsa_x_mem[ESP32_RSA_MEM_BLK_SIZE / 4];

    struct {
        void *rinv;
        bool valid;
    } cache;

    uint32_t rsa_mprime_reg;
    uint32_t rsa_modexp_mode_reg;
    uint32_t rsa_mult_mode_reg;
    uint32_t rsa_clean_reg;
    uint32_t rsa_q_int_reg;
} Esp32RsaState;

REG32(RSA_MEM_M_BLOCK_BASE, 0x000)
REG32(RSA_MEM_RB_BLOCK_BASE, 0x200)
REG32(RSA_MEM_Z_BLOCK_BASE, 0x200)
REG32(RSA_MEM_Y_BLOCK_BASE, 0x400)
REG32(RSA_MEM_X_BLOCK_BASE, 0x600)
REG32(RSA_M_DASH_REG, 0x800)
REG32(RSA_MODEXP_MODE_REG, 0x804)
REG32(RSA_MODEXP_START_REG, 0x808)
REG32(RSA_MULT_MODE_REG, 0x80C)
REG32(RSA_MULT_START_REG, 0x810)
REG32(RSA_CLEAR_INTERRUPT_REG, 0x814)
REG32(RSA_QUERY_INTERRUPT_REG, 0x814)
REG32(RSA_QUERY_CLEAN_REG, 0x818)

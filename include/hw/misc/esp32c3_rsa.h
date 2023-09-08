/*
 * ESP32-C3 RSA accelerator
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32c3_reg.h"


#define TYPE_ESP32C3_RSA "misc.esp32c3.rsa"
#define ESP32C3_RSA(obj) OBJECT_CHECK(ESP32C3RsaState, (obj), TYPE_ESP32C3_RSA)

#define ESP32C3_RSA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3RsaClass, obj, TYPE_ESP32C3_RSA)
#define ESP32C3_RSA_CLASS(klass) OBJECT_CLASS_CHECK(ESP32C3RsaClass, klass, TYPE_ESP32C3_RSA)

#define ESP32C3_RSA_MEM_BLK_SIZE    384

typedef struct ESP32C3RsaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t m_mem[ESP32C3_RSA_MEM_BLK_SIZE / 4];
    uint32_t z_mem[ESP32C3_RSA_MEM_BLK_SIZE / 4];
    uint32_t y_mem[ESP32C3_RSA_MEM_BLK_SIZE / 4];
    uint32_t x_mem[ESP32C3_RSA_MEM_BLK_SIZE / 4];

    /* Configuration registers */
    uint32_t mprime_reg;
    uint32_t mode_reg;
    uint32_t const_time_reg;
    uint32_t search_ena_reg;
    uint32_t search_pos_reg;

    /* Status/Control registers */
    uint32_t int_ena;
    qemu_irq irq;
} ESP32C3RsaState;

typedef struct ESP32C3RsaClass {
    SysBusDeviceClass parent_class;
    /* Virtual methods*/
    void (*rsa_exp_mod)(ESP32C3RsaState *s, uint32_t mode_reg, uint32_t *x_mem, uint32_t *y_mem, uint32_t *m_mem, uint32_t *z_mem, uint32_t int_ena);
} ESP32C3RsaClass;


REG32(RSA_MEM_M_BLOCK_BASE, 0x000)

REG32(RSA_MEM_Z_BLOCK_BASE, 0x200)

REG32(RSA_MEM_Y_BLOCK_BASE, 0x400)

REG32(RSA_MEM_X_BLOCK_BASE, 0x600)

REG32(RSA_M_PRIME_REG, 0x800)

REG32(RSA_MODE_REG, 0x804)
    FIELD(RSA_MODE_REG, RSA_MODE, 0, 7)

REG32(RSA_CLEAN_REG, 0x808)
    FIELD(RSA_CLEAN_REG, RSA_CLEAN, 0, 1)

REG32(RSA_MODEXP_START_REG, 0x80C)
    FIELD(RSA_MODEXP_START_REG, RSA_MODEXP_START, 0, 1)

REG32(RSA_MODMULT_START_REG, 0x810)
    FIELD(RSA_MODMULT_START_REG, RSA_MODMULT_START, 0, 1)

REG32(RSA_MULT_START_REG, 0x814)
    FIELD(RSA_MULT_START_REG, RSA_MULT_START, 0, 1)

REG32(RSA_IDLE_REG, 0x818)
    FIELD(RSA_IDLE_REG, RSA_IDLE, 0, 1)

REG32(RSA_CLEAR_INTERRUPT_REG, 0x81C)
    FIELD(RSA_CLEAR_INTERRUPT_REG, RSA_CLEAR_INTERRUPT, 0, 1)

REG32(RSA_CONSTANT_TIME_REG, 0x820)
    FIELD(RSA_CONSTANT_TIME_REG, RSA_CONSTANT_TIME, 0, 1)

REG32(RSA_SEARCH_ENABLE_REG, 0x824)
    FIELD(RSA_SEARCH_ENABLE_REG, RSA_SEARCH_ENABLE, 0, 1)

REG32(RSA_SEARCH_POS_REG, 0x828)
    FIELD(RSA_SEARCH_POS_REG, RSA_SEARCH_POS, 0, 12)

REG32(RSA_INTERRUPT_ENA_REG, 0x82C)
    FIELD(RSA_INTERRUPT_ENA_REG, RSA_INTERRUPT_ENA, 0, 1)

REG32(RSA_DATE_REG, 0x830)

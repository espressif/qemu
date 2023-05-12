/*
 * ESP32-C3 AES emulation
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
#include "hw/dma/esp32c3_gdma.h"

#define TYPE_ESP32C3_AES "misc.esp32c3.aes"
#define ESP32C3_AES(obj) OBJECT_CHECK(ESP32C3AesState, (obj), TYPE_ESP32C3_AES)

#define ESP32C3_AES_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3AesClass, obj, TYPE_ESP32C3_AES)
#define ESP32C3_AES_CLASS(klass) OBJECT_CLASS_CHECK(ESP32C3AesClass, klass, TYPE_ESP32C3_AES)

#define ESP32C3_AES_REGS_SIZE (A_AES_DMA_EXIT_REG + 4)

#define ESP32C3_AES_TEXT_REG_CNT 4
#define ESP32C3_AES_KEY_REG_CNT  8
#define ESP32C3_AES_IV_REG_CNT   16

#define ESP32C3_AES_IDLE    0
#define ESP32C3_AES_WORK    1
#define ESP32C3_AES_DONE    2

/**
 * Encryption and decryption modes
 */
#define ESP32C3_AES_MODE_128_ENC    0
#define ESP32C3_AES_MODE_256_ENC    2
#define ESP32C3_AES_MODE_128_DEC    4
#define ESP32C3_AES_MODE_256_DEC    6


/**
 * Block Cipher modes
 */
#define ESP32C3_AES_ECB_CIPHER    0
#define ESP32C3_AES_CBC_CIPHER    1
#define ESP32C3_AES_OFB_CIPHER    2
#define ESP32C3_AES_CTR_CIPHER    3
#define ESP32C3_AES_CFB8_CIPHER   4
#define ESP32C3_AES_CFB128_CIPHER 5
#define ESP32C3_AES_CIPHER_COUNT  6


typedef struct ESP32C3AesState {
    SysBusDevice parent_object;
    MemoryRegion iomem;

    uint32_t key[ESP32C3_AES_KEY_REG_CNT];
    uint32_t text_in[ESP32C3_AES_TEXT_REG_CNT];
    uint32_t text_out[ESP32C3_AES_TEXT_REG_CNT];
    uint8_t iv_mem[ESP32C3_AES_IV_REG_CNT];

    uint32_t mode_reg;
    uint32_t state_reg;
    uint32_t dma_enable_reg;
    uint32_t block_mode_reg;
    uint32_t block_num_reg;
    uint32_t inc_sel_reg;

    uint32_t int_ena_reg;
    qemu_irq irq;

    /* Public: must be set by the machine before realizing current instance */
    ESP32C3GdmaState *gdma;
} ESP32C3AesState;


typedef struct ESP32C3AesClass {
    SysBusDeviceClass parent_class;
    /* Virtual methods*/
    void (*aes_block_start)(ESP32C3AesState *s, const uint32_t *key, const uint32_t *text_in, uint32_t *text_out, const uint32_t mode_reg);
} ESP32C3AesClass;


REG32(AES_KEY_0_REG, 0x00)
REG32(AES_KEY_1_REG, 0x04)
REG32(AES_KEY_2_REG, 0x08)
REG32(AES_KEY_3_REG, 0x0C)
REG32(AES_KEY_4_REG, 0x10)
REG32(AES_KEY_5_REG, 0x14)
REG32(AES_KEY_6_REG, 0x18)
REG32(AES_KEY_7_REG, 0x1C)

REG32(AES_TEXT_IN_0_REG, 0x20)
REG32(AES_TEXT_IN_1_REG, 0x24)
REG32(AES_TEXT_IN_2_REG, 0x28)
REG32(AES_TEXT_IN_3_REG, 0x2C)

REG32(AES_TEXT_OUT_0_REG, 0x30)
REG32(AES_TEXT_OUT_1_REG, 0x34)
REG32(AES_TEXT_OUT_2_REG, 0x38)
REG32(AES_TEXT_OUT_3_REG, 0x3C)


REG32(AES_IV_MEM_0_REG,  0x50)
REG32(AES_IV_MEM_1_REG,  0x51)
REG32(AES_IV_MEM_2_REG,  0x52)
REG32(AES_IV_MEM_3_REG,  0x53)
REG32(AES_IV_MEM_4_REG,  0x54)
REG32(AES_IV_MEM_5_REG,  0x55)
REG32(AES_IV_MEM_6_REG,  0x56)
REG32(AES_IV_MEM_7_REG,  0x57)
REG32(AES_IV_MEM_8_REG,  0x58)
REG32(AES_IV_MEM_9_REG,  0x59)
REG32(AES_IV_MEM_10_REG, 0x5a)
REG32(AES_IV_MEM_11_REG, 0x5b)
REG32(AES_IV_MEM_12_REG, 0x5c)
REG32(AES_IV_MEM_13_REG, 0x5d)
REG32(AES_IV_MEM_14_REG, 0x5e)
REG32(AES_IV_MEM_15_REG, 0x5f)


REG32(AES_MODE_REG,      0x40)
    FIELD(AES_MODE_REG, AES_MODE, 0, 3)

REG32(AES_TRIGGER_REG,   0x48)
    FIELD(AES_TRIGGER_REG, AES_TRIGGER, 0, 1)

REG32(AES_STATE_REG,     0x4C)
    FIELD(AES_STATE_REG, AES_STATE, 0, 2)

REG32(AES_DMA_ENA_REG,   0x90)
    FIELD(AES_DMA_ENA_REG, AES_DMA_ENA, 0, 1)

REG32(AES_BLK_MODE_REG,  0x94)
    FIELD(AES_BLK_MODE_REG, AES_BLOCK_MODE, 0, 3)

REG32(AES_BLK_NUM_REG,   0x98)

REG32(AES_INC_SEL_REG,   0x9C)
    FIELD(AES_INC_SEL_REG, AES_INC_SEL, 0, 1)

REG32(AES_INT_CLR_REG,   0xAC)
    FIELD(AES_INT_CLR_REG, AES_INT_CLR, 0, 1)

REG32(AES_INT_ENA_REG,   0xB0)
    FIELD(AES_INT_ENA_REG, AES_INT_ENA, 0, 1)

REG32(AES_DMA_EXIT_REG,  0xB8)

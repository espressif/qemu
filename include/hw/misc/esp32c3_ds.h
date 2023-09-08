/*
 * ESP32-C3 Digital Signature accelerator
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
#include "hw/misc/esp32c3_aes.h"
#include "hw/misc/esp32c3_sha.h"
#include "hw/misc/esp32c3_reg.h"
#include "hw/misc/esp32c3_rsa.h"
#include "hw/misc/esp32c3_hmac.h"


#define TYPE_ESP32C3_DS "misc.esp32c3.ds"
#define ESP32C3_DS(obj) OBJECT_CHECK(ESP32C3DsState, (obj), TYPE_ESP32C3_DS)

#define ESP32C3_DS_KEY_SIZE 32

#define ESP32C3_DS_REGS_SIZE (A_DS_DATE_REG + 4)
#define ESP32C3_DS_MEM_BLK_SIZE 384
#define ESP32C3_DS_BOX_MEM_BLK_SIZE 48
#define ESP32C3_DS_IV_SIZE 16
#define ESP32C3_DS_MPRIME_SIZE 4
#define ESP32C3_DS_L_SIZE 4
#define ESP32C3_DS_MD_SIZE 32

#define ESP32C3_DS_CIPHERTEXT_SIZE (ESP32C3_DS_MEM_BLK_SIZE + \
                                    ESP32C3_DS_MEM_BLK_SIZE + \
                                    ESP32C3_DS_MEM_BLK_SIZE + \
                                    ESP32C3_DS_BOX_MEM_BLK_SIZE)

#define ESP32C3_DS_CALC_MD_SIZE (ESP32C3_DS_MEM_BLK_SIZE + \
                                ESP32C3_DS_MEM_BLK_SIZE + \
                                ESP32C3_DS_MEM_BLK_SIZE + \
                                ESP32C3_DS_MPRIME_SIZE + \
                                ESP32C3_DS_L_SIZE + \
                                ESP32C3_DS_IV_SIZE)

typedef enum {
    DS_SIGNATURE_OK = 0,                    /**< Signature is valid and can be read. */
    DS_SIGNATURE_MD_FAIL = 1,               /**< Message digest check failed, signature invalid. */
    DS_SIGNATURE_PADDING_FAIL = 2,          /**< Padding invalid, signature can be read if user wants it. */
    DS_SIGNATURE_PADDING_AND_MD_FAIL = 3,   /**< Both padding and MD check failed. */
} ds_signature_check_t;


typedef struct ESP32C3DsState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t y_mem[ESP32C3_DS_MEM_BLK_SIZE / 4];
    uint32_t m_mem[ESP32C3_DS_MEM_BLK_SIZE / 4];
    uint32_t rb_mem[ESP32C3_DS_MEM_BLK_SIZE / 4];
    uint32_t box_mem[ESP32C3_DS_BOX_MEM_BLK_SIZE / 4];
    uint32_t x_mem[ESP32C3_DS_MEM_BLK_SIZE / 4];
    uint32_t z_mem[ESP32C3_DS_MEM_BLK_SIZE / 4];
    uint32_t iv[ESP32C3_DS_IV_SIZE / 4];

    uint32_t ds_key[ESP32C3_DS_KEY_SIZE / 4];
    ds_signature_check_t ds_signature_check;

    ESP32C3HmacState *hmac;
    ESP32C3AesState *aes;
    ESP32C3RsaState *rsa;
    ESP32C3ShaState *sha;

} ESP32C3DsState;


REG32(DS_MEM_Y_BLOCK_BASE, 0x0000)
REG32(DS_MEM_M_BLOCK_BASE, 0x0200)
REG32(DS_MEM_RB_BLOCK_BASE, 0x0400)
REG32(DS_MEM_BOX_BLOCK_BASE, 0x0600)
REG32(DS_MEM_X_BLOCK_BASE, 0x0800)
REG32(DS_MEM_Z_BLOCK_BASE, 0x0A00)


REG32(DS_IV_0_REG, 0x0630)
REG32(DS_IV_1_REG, 0x0634)
REG32(DS_IV_2_REG, 0x0638)
REG32(DS_IV_3_REG, 0x063C)


REG32(DS_SET_START_REG, 0xE00)
    FIELD(DS_SET_START_REG, DS_SET_START, 0, 1)

REG32(DS_SET_ME_REG, 0xE04)
    FIELD(DS_SET_ME_REG, DS_SET_ME, 0, 1)

REG32(DS_SET_FINISH_REG, 0xE08)
    FIELD(DS_SET_FINISH_REG, DS_SET_FINISH, 0, 1)

REG32(DS_QUERY_BUSY_REG, 0xE0C)
    FIELD(DS_QUERY_BUSY_REG, DS_QUERY_BUSY, 0, 1)

REG32(DS_QUERY_KEY_WRONG_REG, 0xE10)
    FIELD(DS_QUERY_KEY_WRONG_REG, DS_QUERY_KEY_WRONG, 0, 4)

REG32(DS_QUERY_CHECK_REG, 0xE14)
    FIELD(DS_QUERY_CHECK_REG, DS_MD_ERROR, 0, 1)
    FIELD(DS_QUERY_CHECK_REG, DS_PADDING_BAD, 1, 1)

REG32(DS_DATE_REG, 0xE20)
    FIELD(DS_DATE_REG, DS_DATE, 0, 30)

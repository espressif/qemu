/*
 * ECDSA accelerator of ESP32-H2 and newer chips
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp_ecc.h"

#define TYPE_ESP_ECDSA "misc.esp.ecdsa"
#define ESP_ECDSA(obj) OBJECT_CHECK(ESPEcdsaState, (obj), TYPE_ESP_ECDSA)
#define ESP_ECDSA_GET_CLASS(obj) OBJECT_GET_CLASS(ESPEcdsaClass, obj, TYPE_ESP_ECDSA)
#define ESP_ECDSA_CLASS(klass) OBJECT_CLASS_CHECK(ESPEcdsaClass, klass, TYPE_ESP_ECDSA)

#define TYPE_ESP32C5_ECDSA  "misc.esp32c5.ecdsa"
#define TYPE_ESP32C61_ECDSA "misc.esp32c61.ecdsa"
#define TYPE_ESP32H2_ECDSA  "misc.esp32h2.ecdsa"
#define TYPE_ESP32H4_ECDSA  "misc.esp32h4.ecdsa"
#define TYPE_ESP32P4_ECDSA  "misc.esp32p4.ecdsa"

/* R, S, Z, QAX, QAY, in this order in the register block */
#define ESP_ECDSA_PARAM_COUNT 5

#define ESP_ECDSA_REGS_SIZE 0x1000

REG32(ECDSA_CONF, 0x004)
REG32(ECDSA_INT_RAW, 0x00C)
REG32(ECDSA_INT_ST, 0x010)
REG32(ECDSA_INT_ENA, 0x014)
REG32(ECDSA_INT_CLR, 0x018)
REG32(ECDSA_START, 0x01C)
    FIELD(ECDSA_START, START, 0, 1)
    FIELD(ECDSA_START, LOAD_DONE, 1, 1)
    FIELD(ECDSA_START, GET_DONE, 2, 1)
REG32(ECDSA_STATE, 0x020)
REG32(ECDSA_RESULT, 0x024)
REG32(ECDSA_DATE, 0x0FC)

/* The fields of CONF and the position of the parameter memory differ per chip. */
typedef struct ESPEcdsaLayout {
    uint8_t mode_bits;
    uint8_t curve_shift;
    uint8_t curve_bits;
    uint8_t set_z_shift;
    uint16_t mem_base;
    uint8_t mem_stride;
    uint32_t date;
} ESPEcdsaLayout;

typedef struct ESPEcdsaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t conf;
    uint32_t state;
    uint32_t result;
    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t mem[ESP_ECDSA_PARAM_COUNT][ESP_ECC_MAX_WORDS];
} ESPEcdsaState;

typedef struct ESPEcdsaClass {
    SysBusDeviceClass parent_class;
    const ESPEcdsaLayout *layout;
} ESPEcdsaClass;

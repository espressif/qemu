/*
 * ECC accelerator of ESP32-C6 and newer chips
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

#define TYPE_ESP_ECC "misc.esp.ecc"
#define ESP_ECC(obj) OBJECT_CHECK(ESPEccState, (obj), TYPE_ESP_ECC)
#define ESP_ECC_GET_CLASS(obj) OBJECT_GET_CLASS(ESPEccClass, obj, TYPE_ESP_ECC)
#define ESP_ECC_CLASS(klass) OBJECT_CLASS_CHECK(ESPEccClass, klass, TYPE_ESP_ECC)

#define TYPE_ESP32C5_ECC  "misc.esp32c5.ecc"
#define TYPE_ESP32C6_ECC  "misc.esp32c6.ecc"
#define TYPE_ESP32C61_ECC "misc.esp32c61.ecc"
#define TYPE_ESP32H2_ECC  "misc.esp32h2.ecc"
#define TYPE_ESP32H4_ECC  "misc.esp32h4.ecc"
#define TYPE_ESP32P4_ECC  "misc.esp32p4.ecc"

/* The largest parameter is a P-384 coordinate */
#define ESP_ECC_MAX_BYTES 48
#define ESP_ECC_MAX_WORDS (ESP_ECC_MAX_BYTES / 4)

/* K, PX, PY, QX, QY, QZ. The chips without the extended modes stop at PY. */
#define ESP_ECC_PARAM_MAX 6

REG32(ECC_INT_RAW, 0x00C)
REG32(ECC_INT_ST, 0x010)
REG32(ECC_INT_ENA, 0x014)
REG32(ECC_INT_CLR, 0x018)
REG32(ECC_CONF, 0x01C)
    FIELD(ECC_CONF, START, 0, 1)
    FIELD(ECC_CONF, RESET, 1, 1)
    FIELD(ECC_CONF, KEY_LENGTH, 2, 2)
REG32(ECC_DATE, 0x0FC)
REG32(ECC_MEM, 0x100)

/* The fields of CONF after KEY_LENGTH and the memory stride differ per chip. */
typedef struct ESPEccLayout {
    uint8_t key_length_bits;
    uint8_t work_mode_shift;
    uint8_t work_mode_bits;
    uint8_t result_shift;
    uint8_t param_count;
    uint8_t mem_stride;
    /* With k = 0, ESP32-C61 boards leave PX and PY as they are; other chips return zeros */
    bool mul_zero_keeps_point;
    uint32_t date;
} ESPEccLayout;

typedef struct ESPEccState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t conf;
    bool int_raw;
    bool int_ena;
    uint32_t mem[ESP_ECC_PARAM_MAX][ESP_ECC_MAX_WORDS];
} ESPEccState;

typedef struct ESPEccClass {
    SysBusDeviceClass parent_class;
    const ESPEccLayout *layout;
} ESPEccClass;

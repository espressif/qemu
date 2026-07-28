/*
 * ESP32-P4 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_efuse.h"

#define TYPE_ESP32P4_EFUSE "nvram.esp32p4.efuse"
#define ESP32P4_EFUSE(obj)           OBJECT_CHECK(ESP32P4EfuseState, (obj), TYPE_ESP32P4_EFUSE)
#define ESP32P4_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32P4EfuseClass, obj, TYPE_ESP32P4_EFUSE)
#define ESP32P4_EFUSE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32P4EfuseClass, klass, TYPE_ESP32P4_EFUSE)

/*
 * Bit positions from IDF components/efuse/esp32p4/esp_efuse_table.csv:
 * BLK1 bit 64 WAFER_VERSION_MINOR (4), bit 68 WAFER_VERSION_MAJOR_LO (2),
 * bit 87 WAFER_VERSION_MAJOR_HI (1). BLK1 word 2 is EFUSE_RD_MAC_SYS_2 (0x4C),
 * so the bit offsets within that register are 64 lower.
 */
REG32(EFUSE_P4_RD_MAC_SYS_2, 0x4C)
    FIELD(EFUSE_P4_RD_MAC_SYS_2, WAFER_VER_MINOR, 0, 4)
    FIELD(EFUSE_P4_RD_MAC_SYS_2, WAFER_VER_MAJOR_LO, 4, 2)
    FIELD(EFUSE_P4_RD_MAC_SYS_2, WAFER_VER_MAJOR_HI, 23, 1)

/* Current silicon is v3.1, which is also IDF's default minimum revision */
#define ESP32P4_EFUSE_WAFER_VERSION_MAJOR   3
#define ESP32P4_EFUSE_WAFER_VERSION_MINOR   1

typedef struct ESP32P4EfuseState {
    ESP32C3EfuseState parent;
} ESP32P4EfuseState;

typedef struct ESP32P4EfuseClass {
    ESP32C3EfuseClass parent_class;
    DeviceRealize parent_realize;
} ESP32P4EfuseClass;

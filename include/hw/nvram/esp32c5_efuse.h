/*
 * ESP32-C5 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_efuse.h"

#define TYPE_ESP32C5_EFUSE "nvram.esp32c5.efuse"
#define ESP32C5_EFUSE(obj)           OBJECT_CHECK(ESP32C5EfuseState, (obj), TYPE_ESP32C5_EFUSE)
#define ESP32C5_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5EfuseClass, obj, TYPE_ESP32C5_EFUSE)
#define ESP32C5_EFUSE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5EfuseClass, klass, TYPE_ESP32C5_EFUSE)

/*
 * C5 eFuse BLOCK1 field positions (from esp_efuse_table.csv):
 *   rd_mac_spi_sys_2 (offset 0x4C, BLOCK1 word 2):
 *     WAFER_VERSION_MINOR  [3:0]   (4 bits, BLK1 bit 64)
 *     WAFER_VERSION_MAJOR  [5:4]   (2 bits, BLK1 bit 68)
 *     BLK_VERSION_MINOR    [10:8]  (3 bits, BLK1 bit 72)
 */
REG32(EFUSE_C5_RD_MAC_SYS_2, 0x4C)
    FIELD(EFUSE_C5_RD_MAC_SYS_2, WAFER_VER_MINOR, 0, 4)
    FIELD(EFUSE_C5_RD_MAC_SYS_2, WAFER_VER_MAJOR, 4, 2)
    FIELD(EFUSE_C5_RD_MAC_SYS_2, BLK_VER_MINOR, 8, 3)

typedef struct ESP32C5EfuseState {
    ESP32C3EfuseState parent;
} ESP32C5EfuseState;

typedef struct ESP32C5EfuseClass {
    ESP32C3EfuseClass parent_class;
    void (*parent_realize)(DeviceState *dev, Error **errp);
} ESP32C5EfuseClass;

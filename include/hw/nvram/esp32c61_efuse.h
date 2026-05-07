/*
 * ESP32-C61 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_efuse.h"

#define TYPE_ESP32C61_EFUSE "nvram.esp32c61.efuse"
#define ESP32C61_EFUSE(obj)           OBJECT_CHECK(ESP32C61EfuseState, (obj), TYPE_ESP32C61_EFUSE)
#define ESP32C61_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C61EfuseClass, obj, TYPE_ESP32C61_EFUSE)
#define ESP32C61_EFUSE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C61EfuseClass, klass, TYPE_ESP32C61_EFUSE)

REG32(EFUSE_C61_RD_MAC_SYS_2, 0x4C)
    FIELD(EFUSE_C61_RD_MAC_SYS_2, WAFER_VER_MINOR, 0, 4)
    FIELD(EFUSE_C61_RD_MAC_SYS_2, WAFER_VER_MAJOR, 4, 2)
    FIELD(EFUSE_C61_RD_MAC_SYS_2, BLK_VER_MINOR, 8, 3)

typedef struct ESP32C61EfuseState {
    ESP32C3EfuseState parent;
} ESP32C61EfuseState;

typedef struct ESP32C61EfuseClass {
    ESP32C3EfuseClass parent_class;
    void (*parent_realize)(DeviceState *dev, Error **errp);
} ESP32C61EfuseClass;

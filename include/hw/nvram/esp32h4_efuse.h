/*
 * ESP32-H4 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_efuse.h"

#define TYPE_ESP32H4_EFUSE "nvram.esp32h4.efuse"
#define ESP32H4_EFUSE(obj)           OBJECT_CHECK(ESP32H4EfuseState, (obj), TYPE_ESP32H4_EFUSE)
#define ESP32H4_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4EfuseClass, obj, TYPE_ESP32H4_EFUSE)
#define ESP32H4_EFUSE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4EfuseClass, klass, TYPE_ESP32H4_EFUSE)

REG32(EFUSE_H4_RD_MAC_SYS_3, 0x50)
    FIELD(EFUSE_H4_RD_MAC_SYS_3, WAFER_VER_MINOR, 18, 4)
    FIELD(EFUSE_H4_RD_MAC_SYS_3, WAFER_VER_MAJOR, 22, 2)

typedef struct ESP32H4EfuseState {
    ESP32C3EfuseState parent;
} ESP32H4EfuseState;

typedef struct ESP32H4EfuseClass {
    ESP32C3EfuseClass parent_class;
    void (*parent_realize)(DeviceState *dev, Error **errp);
} ESP32H4EfuseClass;

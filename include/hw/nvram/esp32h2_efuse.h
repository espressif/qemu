/*
 * ESP32-H2 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_efuse.h"

#define TYPE_ESP32H2_EFUSE "nvram.esp32h2.efuse"
#define ESP32H2_EFUSE(obj)           OBJECT_CHECK(ESP32H2EfuseState, (obj), TYPE_ESP32H2_EFUSE)
#define ESP32H2_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H2EfuseClass, obj, TYPE_ESP32H2_EFUSE)
#define ESP32H2_EFUSE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H2EfuseClass, klass, TYPE_ESP32H2_EFUSE)

typedef struct ESP32H2EfuseState {
    ESP32C3EfuseState parent;
} ESP32H2EfuseState;

typedef struct ESP32H2EfuseClass {
    ESP32C3EfuseClass parent_class;
    void (*parent_realize)(DeviceState *dev, Error **errp);
} ESP32H2EfuseClass;

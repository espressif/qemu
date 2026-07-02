/*
 * ESP32-C6 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_efuse.h"

#define TYPE_ESP32C6_EFUSE "nvram.esp32c6.efuse"
#define ESP32C6_EFUSE(obj)           OBJECT_CHECK(ESP32C6EfuseState, (obj), TYPE_ESP32C6_EFUSE)
#define ESP32C6_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6EfuseClass, obj, TYPE_ESP32C6_EFUSE)
#define ESP32C6_EFUSE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6EfuseClass, klass, TYPE_ESP32C6_EFUSE)

typedef struct ESP32C6EfuseState {
    ESP32C3EfuseState parent;
} ESP32C6EfuseState;

typedef struct ESP32C6EfuseClass {
    ESP32C3EfuseClass parent_class;
} ESP32C6EfuseClass;

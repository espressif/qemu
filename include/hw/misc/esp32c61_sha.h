/*
 * ESP32-C61 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c3_sha.h"

#define TYPE_ESP32C61_SHA "misc.esp32c61.sha"
#define ESP32C61_SHA(obj)           OBJECT_CHECK(ESP32C61ShaState, (obj), TYPE_ESP32C61_SHA)
#define ESP32C61_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C61ShaClass, obj, TYPE_ESP32C61_SHA)
#define ESP32C61_SHA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C61ShaClass, klass, TYPE_ESP32C61_SHA)

typedef struct ESP32C61ShaState {
    ESP32C3ShaState parent;
} ESP32C61ShaState;

typedef struct ESP32C61ShaClass {
    ESP32C3ShaClass parent_class;
} ESP32C61ShaClass;

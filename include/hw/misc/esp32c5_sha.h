/*
 * ESP32-C5 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c3_sha.h"

#define TYPE_ESP32C5_SHA "misc.esp32c5.sha"
#define ESP32C5_SHA(obj)           OBJECT_CHECK(ESP32C5ShaState, (obj), TYPE_ESP32C5_SHA)
#define ESP32C5_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5ShaClass, obj, TYPE_ESP32C5_SHA)
#define ESP32C5_SHA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5ShaClass, klass, TYPE_ESP32C5_SHA)

typedef struct ESP32C5ShaState {
    ESP32C3ShaState parent;
} ESP32C5ShaState;

typedef struct ESP32C5ShaClass {
    ESP32C3ShaClass parent_class;
} ESP32C5ShaClass;

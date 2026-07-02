/*
 * ESP32-C6 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c3_sha.h"

#define TYPE_ESP32C6_SHA "misc.esp32c6.sha"
#define ESP32C6_SHA(obj)           OBJECT_CHECK(ESP32C6ShaState, (obj), TYPE_ESP32C6_SHA)
#define ESP32C6_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6ShaClass, obj, TYPE_ESP32C6_SHA)
#define ESP32C6_SHA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6ShaClass, klass, TYPE_ESP32C6_SHA)

typedef struct ESP32C6ShaState {
    ESP32C3ShaState parent;
} ESP32C6ShaState;

typedef struct ESP32C6ShaClass {
    ESP32C3ShaClass parent_class;
} ESP32C6ShaClass;

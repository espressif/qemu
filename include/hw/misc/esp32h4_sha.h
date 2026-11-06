/*
 * ESP32-H4 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c3_sha.h"

#define TYPE_ESP32H4_SHA "misc.esp32h4.sha"
#define ESP32H4_SHA(obj)           OBJECT_CHECK(ESP32H4ShaState, (obj), TYPE_ESP32H4_SHA)
#define ESP32H4_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4ShaClass, obj, TYPE_ESP32H4_SHA)
#define ESP32H4_SHA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4ShaClass, klass, TYPE_ESP32H4_SHA)

typedef struct ESP32H4ShaState {
    ESP32C3ShaState parent;
} ESP32H4ShaState;

typedef struct ESP32H4ShaClass {
    ESP32C3ShaClass parent_class;
} ESP32H4ShaClass;

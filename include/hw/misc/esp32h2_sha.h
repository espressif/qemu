/*
 * ESP32-H2 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c3_sha.h"

#define TYPE_ESP32H2_SHA "misc.esp32h2.sha"
#define ESP32H2_SHA(obj)           OBJECT_CHECK(ESP32H2ShaState, (obj), TYPE_ESP32H2_SHA)
#define ESP32H2_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H2ShaClass, obj, TYPE_ESP32H2_SHA)
#define ESP32H2_SHA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H2ShaClass, klass, TYPE_ESP32H2_SHA)

typedef struct ESP32H2ShaState {
    ESP32C3ShaState parent;
} ESP32H2ShaState;

typedef struct ESP32H2ShaClass {
    ESP32C3ShaClass parent_class;
} ESP32H2ShaClass;

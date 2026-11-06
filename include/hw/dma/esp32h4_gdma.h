/*
 * ESP32-H4 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp_ahb_gdma.h"

#define TYPE_ESP32H4_GDMA "esp32h4.gdma"
#define ESP32H4_GDMA(obj)           OBJECT_CHECK(ESP32H4GdmaState, (obj), TYPE_ESP32H4_GDMA)
#define ESP32H4_GDMA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4GdmaClass, obj, TYPE_ESP32H4_GDMA)
#define ESP32H4_GDMA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4GdmaClass, klass, TYPE_ESP32H4_GDMA)

#define ESP32H4_GDMA_CHANNEL_COUNT  5

typedef struct ESP32H4GdmaState {
    ESPAhbGdmaState parent;
} ESP32H4GdmaState;

typedef struct ESP32H4GdmaClass {
    ESPAhbGdmaClass parent_class;
} ESP32H4GdmaClass;

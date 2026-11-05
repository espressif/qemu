/*
 * ESP32-C5 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp_ahb_gdma.h"

#define TYPE_ESP32C5_GDMA "esp32c5.gdma"
#define ESP32C5_GDMA(obj)           OBJECT_CHECK(ESP32C5GdmaState, (obj), TYPE_ESP32C5_GDMA)
#define ESP32C5_GDMA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5GdmaClass, obj, TYPE_ESP32C5_GDMA)
#define ESP32C5_GDMA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5GdmaClass, klass, TYPE_ESP32C5_GDMA)

#define ESP32C5_GDMA_CHANNEL_COUNT  3

typedef struct ESP32C5GdmaState {
    ESPAhbGdmaState parent;
} ESP32C5GdmaState;

typedef struct ESP32C5GdmaClass {
    ESPAhbGdmaClass parent_class;
} ESP32C5GdmaClass;

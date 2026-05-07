/*
 * ESP32-C61 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp32c3_gdma.h"

#define TYPE_ESP32C61_GDMA "esp32c61.gdma"
#define ESP32C61_GDMA(obj)           OBJECT_CHECK(ESP32C61GdmaState, (obj), TYPE_ESP32C61_GDMA)
#define ESP32C61_GDMA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C61GdmaClass, obj, TYPE_ESP32C61_GDMA)
#define ESP32C61_GDMA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C61GdmaClass, klass, TYPE_ESP32C61_GDMA)

#define ESP32C61_GDMA_CHANNEL_COUNT  2

typedef struct ESP32C61GdmaState {
    ESP32C3GdmaState parent;
} ESP32C61GdmaState;

typedef struct ESP32C61GdmaClass {
    ESP32C3GdmaClass parent_class;
} ESP32C61GdmaClass;

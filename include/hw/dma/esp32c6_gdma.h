/*
 * ESP32-C6 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp32c3_gdma.h"

#define TYPE_ESP32C6_GDMA "esp32c6.gdma"
#define ESP32C6_GDMA(obj)           OBJECT_CHECK(ESP32C6GdmaState, (obj), TYPE_ESP32C6_GDMA)
#define ESP32C6_GDMA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6GdmaClass, obj, TYPE_ESP32C6_GDMA)
#define ESP32C6_GDMA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6GdmaClass, klass, TYPE_ESP32C6_GDMA)

#define ESP32C6_GDMA_CHANNEL_COUNT  ESP32C3_GDMA_CHANNEL_COUNT

typedef struct ESP32C6GdmaState {
    ESP32C3GdmaState parent;
} ESP32C6GdmaState;

typedef struct ESP32C6GdmaClass {
    ESP32C3GdmaClass parent_class;
} ESP32C6GdmaClass;

/*
 * ESP32-H2 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp32c3_gdma.h"

#define TYPE_ESP32H2_GDMA "esp32h2.gdma"
#define ESP32H2_GDMA(obj)           OBJECT_CHECK(ESP32H2GdmaState, (obj), TYPE_ESP32H2_GDMA)
#define ESP32H2_GDMA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H2GdmaClass, obj, TYPE_ESP32H2_GDMA)
#define ESP32H2_GDMA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H2GdmaClass, klass, TYPE_ESP32H2_GDMA)

#define ESP32H2_GDMA_CHANNEL_COUNT  ESP32C3_GDMA_CHANNEL_COUNT

typedef struct ESP32H2GdmaState {
    ESP32C3GdmaState parent;
} ESP32H2GdmaState;

typedef struct ESP32H2GdmaClass {
    ESP32C3GdmaClass parent_class;
} ESP32H2GdmaClass;

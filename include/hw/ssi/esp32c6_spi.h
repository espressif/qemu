/*
 * ESP32-C6 SPI
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/ssi/esp32c3_spi.h"

#define TYPE_ESP32C6_SPI "ssi.esp32c6.spi"
#define ESP32C6_SPI(obj) OBJECT_CHECK(ESP32C6SpiState, (obj), TYPE_ESP32C6_SPI)

typedef struct ESP32C6SpiState {
    ESP32C3SpiState parent;
} ESP32C6SpiState;

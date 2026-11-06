/*
 * ESP32-P4 SPI (SPI1, flash access path)
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/ssi/esp32c3_spi.h"

#define TYPE_ESP32P4_SPI "ssi.esp32p4.spi"
#define ESP32P4_SPI(obj) OBJECT_CHECK(ESP32P4SpiState, (obj), TYPE_ESP32P4_SPI)

typedef struct ESP32P4SpiState {
    ESP32C3SpiState parent;
} ESP32P4SpiState;

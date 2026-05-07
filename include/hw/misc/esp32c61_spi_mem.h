/*
 * ESP32-C61 SPI_MEM (SPI0) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_spi_mem.h"

#define TYPE_ESP32C61_SPI_MEM "esp32c61.spi_mem"
#define ESP32C61_SPI_MEM(obj) OBJECT_CHECK(ESP32C61SpiMemState, (obj), TYPE_ESP32C61_SPI_MEM)

#define ESP32C61_SPI_MEM_BASE    ESP32C5_SPI_MEM_BASE

typedef struct ESP32C61SpiMemState {
    ESP32C5SpiMemState parent;
} ESP32C61SpiMemState;

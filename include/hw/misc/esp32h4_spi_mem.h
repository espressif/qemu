/*
 * ESP32-H4 SPI Memory controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_spi_mem.h"

#define TYPE_ESP32H4_SPI_MEM "esp32h4.spi_mem"
#define ESP32H4_SPI_MEM(obj) OBJECT_CHECK(ESP32H4SpiMemState, (obj), TYPE_ESP32H4_SPI_MEM)

#define ESP32H4_SPI_MEM_BASE    0x60098000

typedef struct ESP32H4SpiMemState {
    ESP32C5SpiMemState parent;
} ESP32H4SpiMemState;

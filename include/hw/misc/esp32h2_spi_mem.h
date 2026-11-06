/*
 * ESP32-H2 SPI_MEM (SPI0) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c6_spi_mem.h"

#define TYPE_ESP32H2_SPI_MEM "esp32h2.spi_mem"

#define ESP32H2_SPI_MEM_IO_SIZE ESP32C6_SPI_MEM_IO_SIZE
#define ESP32H2_SPI_MEM_BASE    ESP32C6_SPI_MEM_BASE

#define H2_MMU_VALID_BIT       C6_MMU_VALID_BIT
#define H2_MMU_PAGE_NUM_MASK   C6_MMU_PAGE_NUM_MASK
#define H2_MMU_ENTRY_COUNT     C6_MMU_ENTRY_COUNT
#define H2_MMU_PAGE_SIZE       C6_MMU_PAGE_SIZE

typedef ESP32C6SpiMemState ESP32H2SpiMemState;

#define ESP32H2_SPI_MEM(obj) \
    OBJECT_CHECK(ESP32H2SpiMemState, (obj), TYPE_ESP32H2_SPI_MEM)

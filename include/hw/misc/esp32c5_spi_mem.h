/*
 * ESP32-C5 SPI_MEM (SPI0) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"
#include "exec/memory.h"
#include "sysemu/block-backend.h"

#define TYPE_ESP32C5_SPI_MEM "esp32c5.spi_mem"
#define ESP32C5_SPI_MEM(obj) OBJECT_CHECK(ESP32C5SpiMemState, (obj), TYPE_ESP32C5_SPI_MEM)

#define ESP32C5_SPI_MEM_IO_SIZE 0x400
#define ESP32C5_SPI_MEM_BASE    0x60002000

#define C5_MMU_VALID_BIT       (1 << 10)
#define C5_MMU_PAGE_NUM_MASK   0x3FF
#define C5_MMU_ENTRY_COUNT     512
#define C5_MMU_PAGE_SIZE       (64 * 1024)

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;

    uint32_t mmu_item_index;
    uint32_t mmu_entries[C5_MMU_ENTRY_COUNT];

    uint32_t mmu_power_ctrl;

    BlockBackend *flash_blk;
    MemoryRegion *dcache_mr;
} ESP32C5SpiMemState;

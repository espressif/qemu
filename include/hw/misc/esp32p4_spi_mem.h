/*
 * ESP32-P4 SPI_MEM (SPI0 / flash MSPI) controller
 *
 * Standalone (not derived from C5): P4 puts TIMING_CALI at a different
 * offset and its flash MMU has 1024 entries with the valid bit at 12.
 * Offsets from IDF: register/hw_ver3/soc/spi_mem_c_reg.h
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
#include "hw/misc/esp32p4_cache.h"

#define TYPE_ESP32P4_SPI_MEM "esp32p4.spi_mem"
#define ESP32P4_SPI_MEM(obj) \
    OBJECT_CHECK(ESP32P4SpiMemState, (obj), TYPE_ESP32P4_SPI_MEM)

#define ESP32P4_SPI_MEM_IO_SIZE 0x1000

/* Flash MMU entry encoding (IDF ext_mem_defs.h) */
#define P4_MMU_VALID_BIT        (1 << 12)
#define P4_MMU_PAGE_NUM_MASK    0x7FF

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;

    uint32_t mmu_item_index;
    uint32_t mmu_entries[ESP32P4_MMU_ENTRY_COUNT];
    uint32_t mmu_power_ctrl;

    BlockBackend *flash_blk;
    MemoryRegion *flash_win_mr;
} ESP32P4SpiMemState;

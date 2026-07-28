/*
 * ESP32-P4 PSRAM MSPI (SPI_MEM_S) controller
 *
 * Separate from the flash SPI0 (SPI_MEM_C): different base, different MMU
 * valid-bit encoding (BIT(11) / 0x3FF page mask vs flash BIT(12) / 0x7FF).
 * Offsets from IDF: register/hw_ver3/soc/spi_mem_s_reg.h
 * Window: SOC_IRAM_PSRAM_ADDRESS_LOW..HIGH = 0x48000000..0x4C000000 (64 MB).
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
#include "hw/misc/esp32p4_cache.h"

#define TYPE_ESP32P4_PSRAM_MEM "esp32p4.psram_mem"
#define ESP32P4_PSRAM_MEM(obj) \
    OBJECT_CHECK(ESP32P4PsramMemState, (obj), TYPE_ESP32P4_PSRAM_MEM)

#define ESP32P4_PSRAM_MEM_IO_SIZE   0x1000
#define ESP32P4_PSRAM_WINDOW_BASE   0x48000000
#define ESP32P4_PSRAM_WINDOW_SIZE   ESP32P4_FLASH_WINDOW_SIZE

/* PSRAM MMU entry encoding (IDF ext_mem_defs.h) */
#define P4_PSRAM_MMU_VALID_BIT      (1 << 11)
#define P4_PSRAM_MMU_PAGE_NUM_MASK  0x3FF

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    MemoryRegion psram;

    uint32_t mmu_item_index;
    uint32_t mmu_entries[ESP32P4_MMU_ENTRY_COUNT];
    uint32_t mmu_power_ctrl;
} ESP32P4PsramMemState;

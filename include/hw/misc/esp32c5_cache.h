/*
 * ESP32-C5 Cache (EXTMEM) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/registerfields.h"
#include "sysemu/block-backend.h"

#define TYPE_ESP32C5_CACHE "esp32c5.cache"
#define ESP32C5_CACHE(obj)           OBJECT_CHECK(ESP32C5CacheState, (obj), TYPE_ESP32C5_CACHE)

#define ESP32C5_DCACHE_BASE 0x42000000
#define ESP32C5_ICACHE_BASE 0x42000000

#define ESP32C5_MMU_ENTRY_COUNT     512
#define ESP32C5_PAGE_SIZE           (64 * 1024)
#define ESP32C5_EXTMEM_REGION_SIZE  (ESP32C5_MMU_ENTRY_COUNT * ESP32C5_PAGE_SIZE)

#define ESP32C5_CACHE_IO_SIZE 0x1000
#define ESP32C5_CACHE_REG_COUNT (0x200 / sizeof(uint32_t))
#define ESP32C5_CACHE_REG_IDX(addr) ((addr) / sizeof(uint32_t))

typedef struct {
    SysBusDevice parent;
    BlockBackend *flash_blk;
    MemoryRegion iomem;

    bool         icache_enable;
    hwaddr       dcache_base;
    hwaddr       icache_base;
    MemoryRegion dcache;

    uint32_t regs[ESP32C5_CACHE_REG_COUNT];
} ESP32C5CacheState;

#define A_EXTMEM_C5_L1_CACHE_CTRL          0x004
#define A_EXTMEM_C5_L1_CACHE_FREEZE_CTRL   0x028
#define A_EXTMEM_C5_L1_CACHE_SYNC_CTRL     0x094
#define A_EXTMEM_C5_L1_CACHE_PRELOAD_CTRL  0x0D4
#define A_EXTMEM_C5_L1_CACHE_AUTOLOAD_CTRL 0x130

#define C5_L1_CACHE_FREEZE_DONE_BIT        (1 << 18)
#define C5_L1_CACHE_FREEZE_EN_BIT          (1 << 16)
#define C5_L1_CACHE_SYNC_DONE_BIT          (1 << 4)
#define C5_L1_CACHE_SYNC_ENA_BIT           (1 << 0)
#define C5_L1_CACHE_PRELOAD_DONE_BIT       (1 << 1)
#define C5_L1_CACHE_PRELOAD_ENA_BIT        (1 << 0)
#define C5_L1_CACHE_AUTOLOAD_DONE_BIT      (1 << 1)
#define C5_L1_CACHE_AUTOLOAD_ENA_BIT       (1 << 0)

/*
 * ESP32-H4 Cache (EXTMEM) controller
 *
 * Standalone (not derived from C5): H4 has dual L1-ICache + L1-DCache with
 * a different register map (SYNC at 0x9c, ICACHE0_AUTOLOAD at 0xe8, etc.).
 * Offsets from IDF: components/soc/esp32h4/register/soc/cache_reg.h
 *
 * Flash window layout matches C5 (shared I/D at 0x42000000, 512 x 64 KB).
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
#include "sysemu/block-backend.h"

#define TYPE_ESP32H4_CACHE "esp32h4.cache"
#define ESP32H4_CACHE(obj) OBJECT_CHECK(ESP32H4CacheState, (obj), TYPE_ESP32H4_CACHE)

#define ESP32H4_DCACHE_BASE         0x42000000
#define ESP32H4_ICACHE_BASE         0x42000000

#define ESP32H4_MMU_ENTRY_COUNT     512
#define ESP32H4_PAGE_SIZE           (64 * 1024)
#define ESP32H4_EXTMEM_REGION_SIZE \
    ((hwaddr)ESP32H4_MMU_ENTRY_COUNT * ESP32H4_PAGE_SIZE)

#define ESP32H4_CACHE_IO_SIZE       0x1000
#define ESP32H4_CACHE_REG_COUNT     (0x400 / sizeof(uint32_t))
#define ESP32H4_CACHE_REG_IDX(addr) ((addr) / sizeof(uint32_t))

#define A_H4_CACHE_L1_ICACHE_CTRL              0x000
#define A_H4_CACHE_L1_DCACHE_CTRL              0x004
#define A_H4_CACHE_L1_FREEZE_CTRL              0x030
#define A_H4_CACHE_LOCK_CTRL                   0x08c
#define A_H4_CACHE_SYNC_CTRL                   0x09c
#define A_H4_CACHE_L1_ICACHE0_PRELOAD_CTRL     0x0ac
#define A_H4_CACHE_L1_ICACHE1_PRELOAD_CTRL     0x0b8
#define A_H4_CACHE_L1_DCACHE_PRELOAD_CTRL      0x0dc
#define A_H4_CACHE_L1_ICACHE0_AUTOLOAD_CTRL    0x0e8
#define A_H4_CACHE_L1_ICACHE1_AUTOLOAD_CTRL    0x0fc
#define A_H4_CACHE_L1_DCACHE_AUTOLOAD_CTRL     0x138

#define H4_CACHE_SYNC_ENA_MASK      0x0000000F
#define H4_CACHE_SYNC_DONE_BIT      (1 << 4)

#define H4_CACHE_LOCK_ENA_MASK      0x00000003
#define H4_CACHE_LOCK_DONE_BIT      (1 << 2)

#define H4_CACHE_PRELOAD_ENA_BIT    (1 << 0)
#define H4_CACHE_PRELOAD_DONE_BIT   (1 << 1)
#define H4_CACHE_AUTOLOAD_DONE_BIT  (1 << 1)

/* FREEZE: ICache0 EN0/DONE2, ICache1 EN4/DONE6, DCache EN16/DONE18 */
#define H4_CACHE_L1_ICACHE0_FREEZE_EN_BIT    (1U << 0)
#define H4_CACHE_L1_ICACHE0_FREEZE_DONE_BIT  (1U << 2)
#define H4_CACHE_L1_ICACHE1_FREEZE_EN_BIT    (1U << 4)
#define H4_CACHE_L1_ICACHE1_FREEZE_DONE_BIT  (1U << 6)
#define H4_CACHE_L1_DCACHE_FREEZE_EN_BIT     (1U << 16)
#define H4_CACHE_L1_DCACHE_FREEZE_DONE_BIT   (1U << 18)

typedef struct {
    SysBusDevice parent;
    BlockBackend *flash_blk;
    MemoryRegion iomem;

    hwaddr       dcache_base;
    MemoryRegion dcache;

    uint32_t regs[ESP32H4_CACHE_REG_COUNT];
} ESP32H4CacheState;

/*
 * ESP32-P4 Cache controller
 *
 * Standalone (not derived from C5): the P4 has a two-level cache
 * (4x L1-ICache + L1-DCache + shared L2) with its own register layout.
 * Offsets from IDF: components/soc/esp32p4/register/hw_ver3/soc/cache_reg.h
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

#define TYPE_ESP32P4_CACHE "esp32p4.cache"
#define ESP32P4_CACHE(obj) OBJECT_CHECK(ESP32P4CacheState, (obj), TYPE_ESP32P4_CACHE)

/*
 * Cached flash window: 1024 MMU entries x 64 KB = 64 MB at 0x40000000
 * (SOC_IRAM_FLASH_ADDRESS_LOW..HIGH in IDF ext_mem_defs.h).
 */
#define ESP32P4_FLASH_WINDOW_BASE   0x40000000
#define ESP32P4_MMU_ENTRY_COUNT     1024
#define ESP32P4_MMU_PAGE_SIZE       (64 * 1024)
#define ESP32P4_FLASH_WINDOW_SIZE \
    ((hwaddr)ESP32P4_MMU_ENTRY_COUNT * ESP32P4_MMU_PAGE_SIZE)

#define ESP32P4_CACHE_IO_SIZE       0x1000
#define ESP32P4_CACHE_REG_COUNT     (0x400 / sizeof(uint32_t))
#define ESP32P4_CACHE_REG_IDX(addr) ((addr) / sizeof(uint32_t))

/* Register offsets from DR_REG_CACHE_BASE (0x3FF10000) */
#define A_P4_CACHE_L1_ICACHE_CTRL           0x000
#define A_P4_CACHE_L1_DCACHE_CTRL           0x004
#define A_P4_CACHE_L1_FREEZE_CTRL           0x02C
#define A_P4_CACHE_LOCK_CTRL                0x088
#define A_P4_CACHE_SYNC_CTRL                0x098
#define A_P4_CACHE_L1_ICACHE0_PRELOAD_CTRL  0x0A8
#define A_P4_CACHE_L1_ICACHE1_PRELOAD_CTRL  0x0B4
#define A_P4_CACHE_L1_ICACHE2_PRELOAD_CTRL  0x0C0
#define A_P4_CACHE_L1_ICACHE3_PRELOAD_CTRL  0x0CC
#define A_P4_CACHE_L1_DCACHE_PRELOAD_CTRL   0x0D8
#define A_P4_CACHE_L1_ICACHE0_AUTOLOAD_CTRL 0x0E4
#define A_P4_CACHE_L1_ICACHE1_AUTOLOAD_CTRL 0x0F8
#define A_P4_CACHE_L1_ICACHE2_AUTOLOAD_CTRL 0x10C
#define A_P4_CACHE_L1_ICACHE3_AUTOLOAD_CTRL 0x120
#define A_P4_CACHE_L1_DCACHE_AUTOLOAD_CTRL  0x134
#define A_P4_CACHE_L2_FREEZE_CTRL           0x28C
#define A_P4_CACHE_L2_PRELOAD_CTRL          0x2A8
#define A_P4_CACHE_L2_AUTOLOAD_CTRL         0x2B4

/* CACHE_SYNC_CTRL_REG: invalidate/clean/writeback/writeback-invalidate */
#define P4_CACHE_SYNC_ENA_MASK      0x0000000F
#define P4_CACHE_SYNC_DONE_BIT      (1 << 4)

/* CACHE_LOCK_CTRL_REG */
#define P4_CACHE_LOCK_ENA_MASK      0x00000003
#define P4_CACHE_LOCK_DONE_BIT      (1 << 2)

/* Preload/autoload control registers */
#define P4_CACHE_PRELOAD_ENA_BIT    (1 << 0)
#define P4_CACHE_PRELOAD_DONE_BIT   (1 << 1)
#define P4_CACHE_AUTOLOAD_DONE_BIT  (1 << 1)

/*
 * CACHE_L1_CACHE_FREEZE_CTRL_REG: per-cache EN at bit 4*n, DONE at 4*n+2
 * (ICache0..3 then DCache).
 */
#define P4_CACHE_L1_FREEZE_UNIT_COUNT   5
#define P4_CACHE_L1_FREEZE_EN(n)        (1U << (4 * (n)))
#define P4_CACHE_L1_FREEZE_DONE(n)      (1U << (4 * (n) + 2))

/* CACHE_L2_CACHE_FREEZE_CTRL_REG */
#define P4_CACHE_L2_FREEZE_EN_BIT       (1U << 20)
#define P4_CACHE_L2_FREEZE_DONE_BIT     (1U << 22)

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;

    /* Backing store for the cached flash window, filled in by SPI_MEM */
    MemoryRegion flash_win;

    uint32_t regs[ESP32P4_CACHE_REG_COUNT];
} ESP32P4CacheState;

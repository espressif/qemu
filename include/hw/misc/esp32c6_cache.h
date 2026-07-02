/*
 * ESP32-C6 Cache (EXTMEM) controller
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

#define TYPE_ESP32C6_CACHE "esp32c6.cache"
#define ESP32C6_CACHE(obj)           OBJECT_CHECK(ESP32C6CacheState, (obj), TYPE_ESP32C6_CACHE)
#define ESP32C6_CACHE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6CacheState, obj, TYPE_ESP32C6_CACHE)
#define ESP32C6_CACHE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6CacheState, klass, TYPE_ESP32C6_CACHE)

/*
 * ESP32-C6 cache region: data and instruction memory share the same
 * cached region (no separate DBUS area like on the ESP32-C3).
 */
#define ESP32C6_CACHE_BASE 0x42000000

/* C6 MMU: 256 entries * 64KB = 16MB region */
#define ESP32C6_MMU_ENTRY_COUNT     256
#define ESP32C6_PAGE_SIZE           (64 * 1024)
#define ESP32C6_EXTMEM_REGION_SIZE  (ESP32C6_MMU_ENTRY_COUNT * ESP32C6_PAGE_SIZE)

/**
 * Size of the Cache I/O register space (same as C3)
 */
#define ESP32C6_CACHE_IO_SIZE 0x1000

/**
 * C6 cache register count (registers span offsets 0x000 to ~0x200)
 */
#define ESP32C6_CACHE_REG_COUNT (0x200 / sizeof(uint32_t))

/**
 * Convert a register address to its index in the registers array
 */
#define ESP32C6_CACHE_REG_IDX(addr) ((addr) / sizeof(uint32_t))

typedef struct {
    SysBusDevice parent;
    BlockBackend *flash_blk;
    MemoryRegion iomem;

    bool         icache_enable;
    hwaddr       cache_base;
    MemoryRegion cache;

    /* Registers for controlling the cache */
    uint32_t regs[ESP32C6_CACHE_REG_COUNT];
} ESP32C6CacheState;


/* ======================================================================
 * ESP32-C6 EXTMEM L1 cache register offsets
 * From ESP-IDF: components/soc/esp32c6/register/soc/extmem_reg.h
 *
 * REG32(...) auto-generates the corresponding `A_*` address macros and
 * `R_*` register-index macros, while FIELD(...) generates the bit shift,
 * mask and `_SHIFT`/`_MASK` helpers used to read/write fields.
 * ====================================================================== */

REG32(EXTMEM_C6_L1_CACHE_CTRL,          0x004)
REG32(EXTMEM_C6_L1_CACHE_FREEZE_CTRL,   0x02C)
    FIELD(EXTMEM_C6_L1_CACHE_FREEZE_CTRL, EN,   16, 1)
    FIELD(EXTMEM_C6_L1_CACHE_FREEZE_CTRL, DONE, 18, 1)
REG32(EXTMEM_C6_L1_CACHE_SYNC_CTRL,     0x098)
    FIELD(EXTMEM_C6_L1_CACHE_SYNC_CTRL, ENA,  0, 1)
    FIELD(EXTMEM_C6_L1_CACHE_SYNC_CTRL, DONE, 4, 1)
REG32(EXTMEM_C6_L1_CACHE_PRELOAD_CTRL,  0x0D8)
    FIELD(EXTMEM_C6_L1_CACHE_PRELOAD_CTRL, ENA,  0, 1)
    FIELD(EXTMEM_C6_L1_CACHE_PRELOAD_CTRL, DONE, 1, 1)
REG32(EXTMEM_C6_L1_CACHE_AUTOLOAD_CTRL, 0x134)
    FIELD(EXTMEM_C6_L1_CACHE_AUTOLOAD_CTRL, ENA,  0, 1)
    FIELD(EXTMEM_C6_L1_CACHE_AUTOLOAD_CTRL, DONE, 1, 1)

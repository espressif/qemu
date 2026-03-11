/*
 * ESP32-H2 Cache (EXTMEM) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c6_cache.h"

/*
 * ESP32-H2 shares the same MMU/cache architecture as ESP32-C6:
 * - Unified cache region at 0x42000000 (IBUS and DBUS share the same 16 MB
 *   address space; there is no separate 0x3C000000 DBUS window like on C3).
 * - 256 MMU entries * 64 KB pages = 16 MB addressable.
 * - Identical EXTMEM L1 cache control register layout.
 *
 * The H2 cache device is a pure QOM subtype of the C6 cache and inherits
 * all register handling logic without modification.
 */

#define TYPE_ESP32H2_CACHE "esp32h2.cache"
#define ESP32H2_CACHE(obj) \
    OBJECT_CHECK(ESP32C6CacheState, (obj), TYPE_ESP32H2_CACHE)
#define ESP32H2_CACHE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ESP32C6CacheState, obj, TYPE_ESP32H2_CACHE)
#define ESP32H2_CACHE_CLASS(klass) \
    OBJECT_CLASS_CHECK(ESP32C6CacheState, klass, TYPE_ESP32H2_CACHE)

typedef ESP32C6CacheState ESP32H2CacheState;

#define ESP32H2_CACHE_BASE          ESP32C6_CACHE_BASE
#define ESP32H2_EXTMEM_REGION_SIZE  ESP32C6_EXTMEM_REGION_SIZE

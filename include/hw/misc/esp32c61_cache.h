/*
 * ESP32-C61 Cache (EXTMEM) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_cache.h"

#define TYPE_ESP32C61_CACHE "esp32c61.cache"
#define ESP32C61_CACHE(obj) OBJECT_CHECK(ESP32C61CacheState, (obj), TYPE_ESP32C61_CACHE)

#define ESP32C61_DCACHE_BASE ESP32C5_DCACHE_BASE
#define ESP32C61_ICACHE_BASE ESP32C5_ICACHE_BASE

typedef struct ESP32C61CacheState {
    ESP32C5CacheState parent;
} ESP32C61CacheState;

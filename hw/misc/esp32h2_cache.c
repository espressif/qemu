/*
 * ESP32-H2 Cache (EXTMEM) controller emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/*
 * The ESP32-H2 uses the same unified cache/MMU architecture as the ESP32-C6:
 * single 16 MB address space at 0x42000000, identical EXTMEM register layout.
 * This file simply registers an H2-specific QOM type that inherits all
 * behavior from the C6 cache controller.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32h2_cache.h"

static const TypeInfo esp32h2_cache_info = {
    .name = TYPE_ESP32H2_CACHE,
    .parent = TYPE_ESP32C6_CACHE,
};

static void esp32h2_cache_register_types(void)
{
    type_register_static(&esp32h2_cache_info);
}

type_init(esp32h2_cache_register_types)

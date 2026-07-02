/*
 * ESP32-C6 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/nvram/esp32c6_efuse.h"

static void esp32c6_efuse_init(Object *obj)
{
}

static const TypeInfo esp32c6_efuse_info = {
    .name = TYPE_ESP32C6_EFUSE,
    .parent = TYPE_ESP32C3_EFUSE,
    .instance_size = sizeof(ESP32C6EfuseState),
    .instance_init = esp32c6_efuse_init,
};

static void esp32c6_efuse_register_types(void)
{
    type_register_static(&esp32c6_efuse_info);
}

type_init(esp32c6_efuse_register_types)

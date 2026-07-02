/*
 * ESP32-C6 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/dma/esp32c6_gdma.h"

static void esp32c6_gdma_init(Object *obj)
{
}

static const TypeInfo esp32c6_gdma_info = {
    .name = TYPE_ESP32C6_GDMA,
    .parent = TYPE_ESP32C3_GDMA,
    .instance_size = sizeof(ESP32C6GdmaState),
    .instance_init = esp32c6_gdma_init,
    .class_size = sizeof(ESP32C6GdmaClass),
};

static void esp32c6_gdma_register_types(void)
{
    type_register_static(&esp32c6_gdma_info);
}

type_init(esp32c6_gdma_register_types)

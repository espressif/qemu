/*
 * ESP32-C61 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/dma/esp32c61_gdma.h"

static void esp32c61_gdma_init(Object *obj)
{
}

static const TypeInfo esp32c61_gdma_info = {
    .name = TYPE_ESP32C61_GDMA,
    .parent = TYPE_ESP32C3_GDMA,
    .instance_size = sizeof(ESP32C61GdmaState),
    .instance_init = esp32c61_gdma_init,
    .class_size = sizeof(ESP32C61GdmaClass),
};

static void esp32c61_gdma_register_types(void)
{
    type_register_static(&esp32c61_gdma_info);
}

type_init(esp32c61_gdma_register_types)

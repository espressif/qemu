/*
 * ESP32-H2 GDMA emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/dma/esp32h2_gdma.h"

static void esp32h2_gdma_init(Object *obj)
{
}

static void esp32h2_gdma_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32h2_gdma_info = {
    .name = TYPE_ESP32H2_GDMA,
    .parent = TYPE_ESP32C3_GDMA,
    .instance_size = sizeof(ESP32H2GdmaState),
    .instance_init = esp32h2_gdma_init,
    .class_init = esp32h2_gdma_class_init,
    .class_size = sizeof(ESP32H2GdmaClass),
};

static void esp32h2_gdma_register_types(void)
{
    type_register_static(&esp32h2_gdma_info);
}

type_init(esp32h2_gdma_register_types)

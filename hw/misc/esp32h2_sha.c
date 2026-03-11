/*
 * ESP32-H2 SHA accelerator emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32h2_sha.h"

static void esp32h2_sha_init(Object *obj)
{
}

static void esp32h2_sha_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32h2_sha_info = {
    .name = TYPE_ESP32H2_SHA,
    .parent = TYPE_ESP32C3_SHA,
    .instance_size = sizeof(ESP32H2ShaState),
    .instance_init = esp32h2_sha_init,
    .class_init = esp32h2_sha_class_init,
    .class_size = sizeof(ESP32H2ShaClass),
};

static void esp32h2_sha_register_types(void)
{
    type_register_static(&esp32h2_sha_info);
}

type_init(esp32h2_sha_register_types)

/*
 * ESP32-H4 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/misc/esp32h4_sha.h"

static void esp32h4_sha_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32h4_sha_info = {
    .name = TYPE_ESP32H4_SHA,
    .parent = TYPE_ESP32C3_SHA,
    .instance_size = sizeof(ESP32H4ShaState),
    .class_init = esp32h4_sha_class_init,
    .class_size = sizeof(ESP32H4ShaClass),
};

static void esp32h4_sha_register_types(void)
{
    type_register_static(&esp32h4_sha_info);
}

type_init(esp32h4_sha_register_types)

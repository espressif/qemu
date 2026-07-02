/*
 * ESP32-C6 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/misc/esp32c6_sha.h"

static void esp32c6_sha_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c6_sha_info = {
    .name = TYPE_ESP32C6_SHA,
    .parent = TYPE_ESP32C3_SHA,
    .instance_size = sizeof(ESP32C6ShaState),
    .class_init = esp32c6_sha_class_init,
    .class_size = sizeof(ESP32C6ShaClass),
};

static void esp32c6_sha_register_types(void)
{
    type_register_static(&esp32c6_sha_info);
}

type_init(esp32c6_sha_register_types)

/*
 * ESP32-C6 AES accelerator, also used by ESP32-C5, ESP32-H2 and ESP32-H4
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/misc/esp32c6_aes.h"

static const TypeInfo esp32c6_aes_info = {
    .name = TYPE_ESP32C6_AES,
    .parent = TYPE_ESP_AES,
    .instance_size = sizeof(ESP32C6AesState),
    .class_size = sizeof(ESP32C6AesClass),
};

static void esp32c6_aes_register_types(void)
{
    type_register_static(&esp32c6_aes_info);
}

type_init(esp32c6_aes_register_types)

/*
 * ESP32-C61 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/timer/esp32c61_timg.h"

static const TypeInfo esp32c61_timg_info = {
    .name = TYPE_ESP32C61_TIMG,
    .parent = TYPE_ESP32C3_TIMG,
    .instance_size = sizeof(ESP32C61TimgState),
    .class_size = sizeof(ESP32C61TimgClass),
};

static void esp32c61_timg_register_types(void)
{
    type_register_static(&esp32c61_timg_info);
}

type_init(esp32c61_timg_register_types)

/*
 * ESP32-C6 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/timer/esp32c6_timg.h"

static const TypeInfo esp32c6_timg_info = {
    .name = TYPE_ESP32C6_TIMG,
    .parent = TYPE_ESP32C3_TIMG,
    .instance_size = sizeof(ESP32C6TimgState),
    .class_size = sizeof(ESP32C6TimgClass),
};

static void esp32c6_timg_register_types(void)
{
    type_register_static(&esp32c6_timg_info);
}

type_init(esp32c6_timg_register_types)

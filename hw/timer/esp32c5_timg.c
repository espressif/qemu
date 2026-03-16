/*
 * ESP32-C5 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/timer/esp32c5_timg.h"

static const TypeInfo esp32c5_timg_info = {
    .name = TYPE_ESP32C5_TIMG,
    .parent = TYPE_ESP32C3_TIMG,
    .instance_size = sizeof(ESP32C5TimgState),
    .class_size = sizeof(ESP32C5TimgClass),
};

static void esp32c5_timg_register_types(void)
{
    type_register_static(&esp32c5_timg_info);
}

type_init(esp32c5_timg_register_types)

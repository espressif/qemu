/*
 * ESP32-C61 INTPRI — software-triggered cross-core interrupts
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32c61_intpri.h"

static const TypeInfo esp32c61_intpri_info = {
    .name = TYPE_ESP32C61_INTPRI,
    .parent = TYPE_ESP32C5_INTPRI,
    .instance_size = sizeof(ESP32C61IntpriState),
};

static void esp32c61_intpri_register_types(void)
{
    type_register_static(&esp32c61_intpri_info);
}

type_init(esp32c61_intpri_register_types)

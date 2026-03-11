/*
 * ESP32-H2 INTPRI -- software-triggered cross-core interrupts
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/*
 * The ESP32-H2 INTPRI block is identical to the ESP32-C6's.
 * This file registers an H2-specific QOM type that inherits all behavior.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32h2_intpri.h"

static const TypeInfo esp32h2_intpri_info = {
    .name = TYPE_ESP32H2_INTPRI,
    .parent = TYPE_ESP32C6_INTPRI,
};

static void esp32h2_intpri_register_types(void)
{
    type_register_static(&esp32h2_intpri_info);
}

type_init(esp32h2_intpri_register_types)

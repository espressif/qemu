/*
 * ESP32-C5 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/riscv/esp32c5_clk.h"

static void esp32c5_clock_init(Object *obj)
{
}

static void esp32c5_clock_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c5_clock_info = {
    .name = TYPE_ESP32C5_CLOCK,
    .parent = TYPE_ESP32C3_CLOCK,
    .instance_size = sizeof(ESP32C5ClockState),
    .instance_init = esp32c5_clock_init,
    .class_init = esp32c5_clock_class_init,
    .class_size = sizeof(ESP32C5ClockClass),
};

static void esp32c5_clock_register_types(void)
{
    type_register_static(&esp32c5_clock_info);
}

type_init(esp32c5_clock_register_types)

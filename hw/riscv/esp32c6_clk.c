/*
 * ESP32-C6 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/riscv/esp32c6_clk.h"

static void esp32c6_clock_init(Object *obj)
{
}

static void esp32c6_clock_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c6_clock_info = {
    .name = TYPE_ESP32C6_CLOCK,
    .parent = TYPE_ESP32C3_CLOCK,
    .instance_size = sizeof(ESP32C6ClockState),
    .instance_init = esp32c6_clock_init,
    .class_init = esp32c6_clock_class_init,
    .class_size = sizeof(ESP32C6ClockClass),
};

static void esp32c6_clock_register_types(void)
{
    type_register_static(&esp32c6_clock_info);
}

type_init(esp32c6_clock_register_types)

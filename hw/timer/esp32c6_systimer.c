/*
 * ESP32-C6 System Timer
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/timer/esp32c6_systimer.h"

static void esp32c6_systimer_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c6_systimer_info = {
    .name = TYPE_ESP32C6_SYSTIMER,
    .parent = TYPE_ESP32C3_SYSTIMER,
    .instance_size = sizeof(ESP32C6SysTimerState),
    .class_init = esp32c6_systimer_class_init,
    .class_size = sizeof(ESP32C6SysTimerClass),
};

static void esp32c6_systimer_register_types(void)
{
    type_register_static(&esp32c6_systimer_info);
}

type_init(esp32c6_systimer_register_types)

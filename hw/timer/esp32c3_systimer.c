/*
 * ESP32-C3 System Timer
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/timer/esp32c3_systimer.h"


static uint64_t esp32c3_systimer_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3SysTimerClass *class = ESP32C3_SYSTIMER_GET_CLASS(opaque);
    uint64_t r = 0;

    switch (addr) {
        case A_SYSTIMER_CONF:
            /* On the C3, bit 27 is always 0, bit 25 is always high */
            r = class->parent_systimer_read(opaque, addr, size);
            r &= ~BIT(27);
            r |=  BIT(25);
            break;

        case A_SYSTIMER_REAL_TARGET0_LO:
        case A_SYSTIMER_REAL_TARGET0_HI:
        case A_SYSTIMER_REAL_TARGET1_LO:
        case A_SYSTIMER_REAL_TARGET1_HI:
        case A_SYSTIMER_REAL_TARGET2_LO:
        case A_SYSTIMER_REAL_TARGET2_HI:
            /* These registers are not supported on the C3 hardware, do nothing! */
            break;

        default:
            r = class->parent_systimer_read(opaque, addr, size);
            break;
    }

    return r;
}

static void esp32c3_systimer_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESP32C3SysTimerClass *class = ESP32C3_SYSTIMER_GET_CLASS(opaque);
    ESP32C3SysTimerState *s = ESP32C3_SYSTIMER(opaque);

    class->parent_systimer_write(opaque, addr, value, size);

    if (addr == A_SYSTIMER_INT_ENA) {
        /* If interrupt enable is set after comparator programming, force a
         * re-evaluation so "alarm already passed" cases are not missed.
         */
        class->parent_class.comparators_reprogram(&s->parent);
    }
}


static void esp32c3_systimer_class_init(ObjectClass *klass, void *data)
{
    ESP32C3SysTimerClass* esp32c3 = ESP32C3_SYSTIMER_CLASS(klass);
    ESPSysTimerClass* esp = ESP_SYSTIMER_CLASS(klass);

    /* Override register read/write methods */
    esp32c3->parent_systimer_read = esp->systimer_ops.read;
    esp32c3->parent_systimer_write = esp->systimer_ops.write;
    esp->systimer_ops.read = esp32c3_systimer_read;
    esp->systimer_ops.write = esp32c3_systimer_write;
}


static const TypeInfo esp32c3_systimer_info = {
    .name = TYPE_ESP32C3_SYSTIMER,
    .parent = TYPE_ESP_SYSTIMER,
    .instance_size = sizeof(ESP32C3SysTimerState),
    .class_init = esp32c3_systimer_class_init,
    .class_size = sizeof(ESP32C3SysTimerClass),
};


static void esp32c3_systimer_register_types(void)
{
    type_register_static(&esp32c3_systimer_info);
}


type_init(esp32c3_systimer_register_types)

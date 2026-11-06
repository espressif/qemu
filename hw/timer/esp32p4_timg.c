/*
 * ESP32-P4 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/timer/esp32p4_timg.h"

static void esp32p4_timg_class_init(ObjectClass *klass, void *data)
{
    ESPTimgClass *esp = ESP_TIMG_CLASS(klass);
    /*
     * Each P4 timer group has two general purpose timers, so the interrupt
     * registers are laid out as WDT | T1 | T0 instead of the C3's WDT | T0.
     */
    esp->m_has_t1 = true;
}

static const TypeInfo esp32p4_timg_info = {
    .name = TYPE_ESP32P4_TIMG,
    .parent = TYPE_ESP32C3_TIMG,
    .instance_size = sizeof(ESP32P4TimgState),
    .class_size = sizeof(ESP32P4TimgClass),
    .class_init = esp32p4_timg_class_init,
};

static void esp32p4_timg_register_types(void)
{
    type_register_static(&esp32p4_timg_info);
}

type_init(esp32p4_timg_register_types)

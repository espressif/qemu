/*
 * ESP32-C5 GPIO emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/gpio/esp32c5_gpio.h"

static void esp32c5_gpio_init(Object *obj)
{
    Esp32GpioState *s = ESP32_GPIO(obj);
    object_property_set_int(obj, "strap_mode", ESP32C5_STRAP_MODE_FLASH_BOOT, &error_fatal);
    s->strap_offset = 0x0;
    s->num_gpios = 29;

    /* ESP32-C5 GPIO register offsets */
    s->out_off        = 0x04;
    s->out_w1ts_off   = 0x08;
    s->out_w1tc_off   = 0x0c;
    s->enable_off     = 0x34;
    s->enable_w1ts_off = 0x38;
    s->enable_w1tc_off = 0x3c;
    s->in_off         = 0x64;
}

static void esp32c5_gpio_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c5_gpio_info = {
    .name = TYPE_ESP32C5_GPIO,
    .parent = TYPE_ESP32C3_GPIO,
    .instance_size = sizeof(ESP32C5GPIOState),
    .instance_init = esp32c5_gpio_init,
    .class_init = esp32c5_gpio_class_init,
    .class_size = sizeof(ESP32C5GPIOClass),
};

static void esp32c5_gpio_register_types(void)
{
    type_register_static(&esp32c5_gpio_info);
}

type_init(esp32c5_gpio_register_types)

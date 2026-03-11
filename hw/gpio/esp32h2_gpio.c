/*
 * ESP32-H2 GPIO emulation
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
#include "hw/gpio/esp32h2_gpio.h"

static void esp32h2_gpio_init(Object *obj)
{
    object_property_set_int(obj, "strap_mode", ESP32H2_STRAP_MODE_FLASH_BOOT, &error_fatal);
}

static void esp32h2_gpio_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32h2_gpio_info = {
    .name = TYPE_ESP32H2_GPIO,
    .parent = TYPE_ESP32C3_GPIO,
    .instance_size = sizeof(ESP32H2GPIOState),
    .instance_init = esp32h2_gpio_init,
    .class_init = esp32h2_gpio_class_init,
    .class_size = sizeof(ESP32H2GPIOClass),
};

static void esp32h2_gpio_register_types(void)
{
    type_register_static(&esp32h2_gpio_info);
}

type_init(esp32h2_gpio_register_types)

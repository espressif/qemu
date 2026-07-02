/*
 * ESP32-C6 GPIO emulation
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
#include "hw/gpio/esp32c6_gpio.h"

static void esp32c6_gpio_init(Object *obj)
{
    object_property_set_int(obj, "strap_mode", ESP32C6_STRAP_MODE_FLASH_BOOT, &error_fatal);
}

static void esp32c6_gpio_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c6_gpio_info = {
    .name = TYPE_ESP32C6_GPIO,
    .parent = TYPE_ESP32C3_GPIO,
    .instance_size = sizeof(ESP32C6GPIOState),
    .instance_init = esp32c6_gpio_init,
    .class_init = esp32c6_gpio_class_init,
    .class_size = sizeof(ESP32C6GPIOClass),
};

static void esp32c6_gpio_register_types(void)
{
    type_register_static(&esp32c6_gpio_info);
}

type_init(esp32c6_gpio_register_types)

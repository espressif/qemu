/*
 * ESP32-C3 GPIO emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32c3_gpio.h"


static void esp32c3_gpio_init(Object *obj)
{
    /* Set the default value for the property */
    object_property_set_int(obj, "strap_mode", ESP32C3_STRAP_MODE_FLASH_BOOT, &error_fatal);
}

/* If we need to override any function from the parent (reset, realize, ...), it shall be done
 * in this class_init function */
static void esp32c3_gpio_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c3_gpio_info = {
    .name = TYPE_ESP32C3_GPIO,
    .parent = TYPE_ESP32_GPIO,
    .instance_size = sizeof(ESP32C3GPIOState),
    .instance_init = esp32c3_gpio_init,
    .class_init = esp32c3_gpio_class_init,
    .class_size = sizeof(ESP32C3GPIOClass),
};

static void esp32c3_gpio_register_types(void)
{
    type_register_static(&esp32c3_gpio_info);
}

type_init(esp32c3_gpio_register_types)

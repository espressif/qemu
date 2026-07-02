/*
 * ESP32-C6 UART emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/char/esp32c6_uart.h"

static void esp32c6_uart_init(Object *obj)
{
}

static void esp32c6_uart_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c6_uart_info = {
    .name = TYPE_ESP32C6_UART,
    .parent = TYPE_ESP32C3_UART,
    .instance_size = sizeof(ESP32C6UARTState),
    .instance_init = esp32c6_uart_init,
    .class_init = esp32c6_uart_class_init,
    .class_size = sizeof(ESP32C6UARTClass),
};

static void esp32c6_uart_register_types(void)
{
    type_register_static(&esp32c6_uart_info);
}

type_init(esp32c6_uart_register_types)

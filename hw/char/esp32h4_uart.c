/*
 * ESP32-H4 UART
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/char/esp32h4_uart.h"

static const TypeInfo esp32h4_uart_info = {
    .name = TYPE_ESP32H4_UART,
    .parent = TYPE_ESP32C3_UART,
    .instance_size = sizeof(ESP32H4UARTState),
    .class_size = sizeof(ESP32H4UARTClass),
};

static void esp32h4_uart_register_types(void)
{
    type_register_static(&esp32h4_uart_info);
}

type_init(esp32h4_uart_register_types)

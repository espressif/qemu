/*
 * ESP32-H2 UART emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/char/esp32h2_uart.h"

static void esp32h2_uart_init(Object *obj)
{
}

static void esp32h2_uart_class_init(ObjectClass *klass, void *data)
{
    ESP32C3_UART_CLASS(klass)->rxd_cnt_reg = ESP32C6_UART_RXD_CNT_REG;
}

static const TypeInfo esp32h2_uart_info = {
    .name = TYPE_ESP32H2_UART,
    .parent = TYPE_ESP32C3_UART,
    .instance_size = sizeof(ESP32H2UARTState),
    .instance_init = esp32h2_uart_init,
    .class_init = esp32h2_uart_class_init,
    .class_size = sizeof(ESP32H2UARTClass),
};

static void esp32h2_uart_register_types(void)
{
    type_register_static(&esp32h2_uart_info);
}

type_init(esp32h2_uart_register_types)

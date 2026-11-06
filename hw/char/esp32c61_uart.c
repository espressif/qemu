/*
 * ESP32-C61 UART
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/char/esp32c61_uart.h"

static void esp32c61_uart_class_init(ObjectClass *klass, void *data)
{
    ESP32C3_UART_CLASS(klass)->rxd_cnt_reg = ESP32C6_UART_RXD_CNT_REG;
    ESP32_UART_CLASS(klass)->rxfifo_rst_mask = R_ESP32C6_UART_CONF0_SYNC_RXFIFO_RST_MASK;
}

static const TypeInfo esp32c61_uart_info = {
    .name = TYPE_ESP32C61_UART,
    .parent = TYPE_ESP32C3_UART,
    .instance_size = sizeof(ESP32C61UARTState),
    .class_init = esp32c61_uart_class_init,
    .class_size = sizeof(ESP32C61UARTClass),
};

static void esp32c61_uart_register_types(void)
{
    type_register_static(&esp32c61_uart_info);
}

type_init(esp32c61_uart_register_types)

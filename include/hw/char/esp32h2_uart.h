/*
 * ESP32-H2 UART
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_uart.h"

#define TYPE_ESP32H2_UART "esp32h2_soc.uart"
#define ESP32H2_UART(obj)           OBJECT_CHECK(ESP32H2UARTState, (obj), TYPE_ESP32H2_UART)
#define ESP32H2_UART_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H2UARTClass, obj, TYPE_ESP32H2_UART)
#define ESP32H2_UART_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H2UARTClass, klass, TYPE_ESP32H2_UART)

#define ESP32H2_UART_IO_SIZE 0x100

typedef struct ESP32H2UARTState {
    ESP32C3UARTState parent;

    /* Registers that have no counterpart at the same offset on the C3 */
    uint32_t regs[ESP32H2_UART_IO_SIZE / sizeof(uint32_t)];
} ESP32H2UARTState;

typedef struct ESP32H2UARTClass {
    ESP32C3UARTClass parent_class;

    void (*parent_uart_write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
    uint64_t (*parent_uart_read)(void *opaque, hwaddr addr, unsigned int size);
} ESP32H2UARTClass;

/*
 * Registers whose address or field layout differs from the ESP32-C3, from IDF
 * components/soc/esp32h2/register/soc/uart_reg.h.
 */
REG32(ESP32H2_UART_RX_FILT, 0x18)
REG32(ESP32H2_UART_CONF1, 0x24)
    FIELD(ESP32H2_UART_CONF1, TXFIFO_EMPTY_THRHD, 8, 8)
    FIELD(ESP32H2_UART_CONF1, RXFIFO_FULL_THRHD, 0, 8)
REG32(ESP32H2_UART_HWFC_CONF, 0x2c)
REG32(ESP32H2_UART_SLEEP_CONF0, 0x30)
REG32(ESP32H2_UART_MEM_CONF, 0x60)
REG32(ESP32H2_UART_TOUT_CONF, 0x64)
    FIELD(ESP32H2_UART_TOUT_CONF, RX_TOUT_THRHD, 2, 10)
    FIELD(ESP32H2_UART_TOUT_CONF, RX_TOUT_EN, 0, 1)
REG32(ESP32H2_UART_LOWPULSE, 0x7c)
REG32(ESP32H2_UART_HIGHPULSE, 0x80)
REG32(ESP32H2_UART_CLK_CONF, 0x88)
REG32(ESP32H2_UART_DATE, 0x8c)
REG32(ESP32H2_UART_AFIFO_STATUS, 0x90)
REG32(ESP32H2_UART_ID, 0x9c)

/*
 * ESP32-P4 UART
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_uart.h"

#define TYPE_ESP32P4_UART "esp32p4_soc.uart"
#define ESP32P4_UART(obj)           OBJECT_CHECK(ESP32P4UARTState, (obj), TYPE_ESP32P4_UART)
#define ESP32P4_UART_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32P4UARTClass, obj, TYPE_ESP32P4_UART)
#define ESP32P4_UART_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32P4UARTClass, klass, TYPE_ESP32P4_UART)

typedef struct ESP32P4UARTState {
    ESP32C3UARTState parent;

    uint32_t tout_conf;
} ESP32P4UARTState;

typedef struct ESP32P4UARTClass {
    ESP32C3UARTClass parent_class;

    void (*parent_uart_write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
    uint64_t (*parent_uart_read)(void *opaque, hwaddr addr, unsigned int size);
} ESP32P4UARTClass;

/*
 * Registers whose address or field layout differs from the ESP32-C3, from IDF
 * components/soc/esp32p4/register/hw_ver3/soc/uart_reg.h. Most notably the RX
 * timeout configuration moved out of CONF1 into its own register, and the FIFO
 * threshold fields shrank from 9 to 8 bits.
 */
REG32(ESP32P4_UART_CONF0, 0x20)
    FIELD(ESP32P4_UART_CONF0, AUTOBAUD_EN, 19, 1)

REG32(ESP32P4_UART_CONF1, 0x24)
    FIELD(ESP32P4_UART_CONF1, TXFIFO_EMPTY_THRHD, 8, 8)
    FIELD(ESP32P4_UART_CONF1, RXFIFO_FULL_THRHD, 0, 8)

REG32(ESP32P4_UART_MEM_CONF, 0x60)

REG32(ESP32P4_UART_TOUT_CONF, 0x64)
    FIELD(ESP32P4_UART_TOUT_CONF, RX_TOUT_THRHD, 2, 10)
    FIELD(ESP32P4_UART_TOUT_CONF, RX_TOUT_EN, 0, 1)

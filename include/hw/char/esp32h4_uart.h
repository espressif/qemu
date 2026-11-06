/*
 * ESP32-H4 UART
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_uart.h"

#define TYPE_ESP32H4_UART "esp32h4_soc.uart"
#define ESP32H4_UART(obj)           OBJECT_CHECK(ESP32H4UARTState, (obj), TYPE_ESP32H4_UART)
#define ESP32H4_UART_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4UARTClass, obj, TYPE_ESP32H4_UART)
#define ESP32H4_UART_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4UARTClass, klass, TYPE_ESP32H4_UART)

typedef struct ESP32H4UARTState {
    ESP32C3UARTState parent;
} ESP32H4UARTState;

typedef struct ESP32H4UARTClass {
    ESP32C3UARTClass parent_class;
} ESP32H4UARTClass;

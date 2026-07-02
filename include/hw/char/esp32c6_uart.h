/*
 * ESP32-C6 UART
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_uart.h"

#define TYPE_ESP32C6_UART "esp32c6_soc.uart"
#define ESP32C6_UART(obj)           OBJECT_CHECK(ESP32C6UARTState, (obj), TYPE_ESP32C6_UART)
#define ESP32C6_UART_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6UARTClass, obj, TYPE_ESP32C6_UART)
#define ESP32C6_UART_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6UARTClass, klass, TYPE_ESP32C6_UART)

typedef struct ESP32C6UARTState {
    ESP32C3UARTState parent;
} ESP32C6UARTState;

typedef struct ESP32C6UARTClass {
    ESP32C3UARTClass parent_class;
} ESP32C6UARTClass;

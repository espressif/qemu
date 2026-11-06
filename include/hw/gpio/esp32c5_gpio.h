/*
 * ESP32-C5 GPIO
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_gpio.h"

#define TYPE_ESP32C5_GPIO "esp32c5.gpio"
#define ESP32C5_GPIO(obj)           OBJECT_CHECK(ESP32C5GPIOState, (obj), TYPE_ESP32C5_GPIO)
#define ESP32C5_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5GPIOClass, obj, TYPE_ESP32C5_GPIO)
#define ESP32C5_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5GPIOClass, klass, TYPE_ESP32C5_GPIO)

#define ESP32C5_STRAP_MODE_FLASH_BOOT 0x1c

typedef struct ESP32C5GPIOState {
    ESP32C3GPIOState parent;
} ESP32C5GPIOState;

typedef struct ESP32C5GPIOClass {
    ESP32C3GPIOClass parent_class;
} ESP32C5GPIOClass;

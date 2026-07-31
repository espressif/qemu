/*
 * ESP32-H4 GPIO
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_gpio.h"

#define TYPE_ESP32H4_GPIO "esp32h4.gpio"
#define ESP32H4_GPIO(obj)           OBJECT_CHECK(ESP32H4GPIOState, (obj), TYPE_ESP32H4_GPIO)
#define ESP32H4_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4GPIOClass, obj, TYPE_ESP32H4_GPIO)
#define ESP32H4_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4GPIOClass, klass, TYPE_ESP32H4_GPIO)

#define ESP32H4_STRAP_MODE_FLASH_BOOT 0x1c

typedef struct ESP32H4GPIOState {
    ESP32C3GPIOState parent;
} ESP32H4GPIOState;

typedef struct ESP32H4GPIOClass {
    ESP32C3GPIOClass parent_class;
} ESP32H4GPIOClass;

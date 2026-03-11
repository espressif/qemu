/*
 * ESP32-H2 GPIO
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_gpio.h"

#define TYPE_ESP32H2_GPIO "esp32h2.gpio"
#define ESP32H2_GPIO(obj)           OBJECT_CHECK(ESP32H2GPIOState, (obj), TYPE_ESP32H2_GPIO)
#define ESP32H2_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H2GPIOClass, obj, TYPE_ESP32H2_GPIO)
#define ESP32H2_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H2GPIOClass, klass, TYPE_ESP32H2_GPIO)

#define ESP32H2_STRAP_MODE_FLASH_BOOT 0xc

typedef struct ESP32H2GPIOState {
    ESP32C3GPIOState parent;
} ESP32H2GPIOState;

typedef struct ESP32H2GPIOClass {
    ESP32C3GPIOClass parent_class;
} ESP32H2GPIOClass;

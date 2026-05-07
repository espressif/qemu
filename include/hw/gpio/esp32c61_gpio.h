/*
 * ESP32-C61 GPIO
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_gpio.h"

#define TYPE_ESP32C61_GPIO "esp32c61.gpio"
#define ESP32C61_GPIO(obj)           OBJECT_CHECK(ESP32C61GPIOState, (obj), TYPE_ESP32C61_GPIO)
#define ESP32C61_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C61GPIOClass, obj, TYPE_ESP32C61_GPIO)
#define ESP32C61_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C61GPIOClass, klass, TYPE_ESP32C61_GPIO)

#define ESP32C61_STRAP_MODE_FLASH_BOOT 0x1c

typedef struct ESP32C61GPIOState {
    ESP32C3GPIOState parent;
} ESP32C61GPIOState;

typedef struct ESP32C61GPIOClass {
    ESP32C3GPIOClass parent_class;
} ESP32C61GPIOClass;

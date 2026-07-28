/*
 * ESP32-P4 GPIO
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_gpio.h"

#define TYPE_ESP32P4_GPIO "esp32p4.gpio"
#define ESP32P4_GPIO(obj)           OBJECT_CHECK(ESP32P4GPIOState, (obj), TYPE_ESP32P4_GPIO)
#define ESP32P4_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32P4GPIOClass, obj, TYPE_ESP32P4_GPIO)
#define ESP32P4_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32P4GPIOClass, klass, TYPE_ESP32P4_GPIO)

/* bit3 set for SPI_FAST_FLASH_BOOT */
#define ESP32P4_STRAP_MODE_FLASH_BOOT 0x30f

typedef struct ESP32P4GPIOState {
    ESP32C3GPIOState parent;
} ESP32P4GPIOState;

typedef struct ESP32P4GPIOClass {
    ESP32C3GPIOClass parent_class;
} ESP32P4GPIOClass;

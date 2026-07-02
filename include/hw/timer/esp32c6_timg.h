/*
 * ESP32-C6 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_timg.h"

#define TYPE_ESP32C6_TIMG           "timer.esp32c6.timg"
#define ESP32C6_TIMG(obj)           OBJECT_CHECK(ESP32C6TimgState, (obj), TYPE_ESP32C6_TIMG)
#define ESP32C6_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6TimgClass, obj, TYPE_ESP32C6_TIMG)
#define ESP32C6_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6TimgClass, klass, TYPE_ESP32C6_TIMG)

#define ESP32C6_T0_IRQ_INTERRUPT        ESP_T0_IRQ_INTERRUPT
#define ESP32C6_WDT_IRQ_INTERRUPT       ESP_WDT_IRQ_INTERRUPT
#define ESP32C6_WDT_IRQ_RESET           ESP_WDT_IRQ_RESET

typedef struct ESP32C6TimgState {
    ESP32C3TimgState parent;
} ESP32C6TimgState;

typedef struct ESP32C6TimgClass {
    ESP32C3TimgClass parent_class;
} ESP32C6TimgClass;

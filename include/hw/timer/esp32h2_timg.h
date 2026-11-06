/*
 * ESP32-H2 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_timg.h"

#define TYPE_ESP32H2_TIMG           "timer.esp32h2.timg"
#define ESP32H2_TIMG(obj)           OBJECT_CHECK(ESP32H2TimgState, (obj), TYPE_ESP32H2_TIMG)
#define ESP32H2_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H2TimgClass, obj, TYPE_ESP32H2_TIMG)
#define ESP32H2_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H2TimgClass, klass, TYPE_ESP32H2_TIMG)

#define ESP32H2_T0_IRQ_INTERRUPT        ESP_T0_IRQ_INTERRUPT
#define ESP32H2_WDT_IRQ_INTERRUPT       ESP_WDT_IRQ_INTERRUPT
#define ESP32H2_WDT_IRQ_RESET           ESP_WDT_IRQ_RESET

typedef ESP32C3TimgState ESP32H2TimgState;
typedef ESP32C3TimgClass ESP32H2TimgClass;

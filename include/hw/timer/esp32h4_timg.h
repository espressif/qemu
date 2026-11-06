/*
 * ESP32-H4 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_timg.h"

#define TYPE_ESP32H4_TIMG           "timer.esp32h4.timg"
#define ESP32H4_TIMG(obj)           OBJECT_CHECK(ESP32H4TimgState, (obj), TYPE_ESP32H4_TIMG)
#define ESP32H4_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4TimgClass, obj, TYPE_ESP32H4_TIMG)
#define ESP32H4_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4TimgClass, klass, TYPE_ESP32H4_TIMG)

#define ESP32H4_T0_IRQ_INTERRUPT        ESP_T0_IRQ_INTERRUPT
#define ESP32H4_WDT_IRQ_INTERRUPT       ESP_WDT_IRQ_INTERRUPT
#define ESP32H4_WDT_IRQ_RESET           ESP_WDT_IRQ_RESET

typedef ESP32C3TimgState ESP32H4TimgState;
typedef ESP32C3TimgClass ESP32H4TimgClass;

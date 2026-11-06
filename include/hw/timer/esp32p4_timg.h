/*
 * ESP32-P4 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_timg.h"

#define TYPE_ESP32P4_TIMG           "timer.esp32p4.timg"
#define ESP32P4_TIMG(obj)           OBJECT_CHECK(ESP32P4TimgState, (obj), TYPE_ESP32P4_TIMG)
#define ESP32P4_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32P4TimgClass, obj, TYPE_ESP32P4_TIMG)
#define ESP32P4_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32P4TimgClass, klass, TYPE_ESP32P4_TIMG)

#define ESP32P4_T0_IRQ_INTERRUPT        ESP_T0_IRQ_INTERRUPT
#define ESP32P4_T1_IRQ_INTERRUPT        ESP_T1_IRQ_INTERRUPT
#define ESP32P4_WDT_IRQ_INTERRUPT       ESP_WDT_IRQ_INTERRUPT
#define ESP32P4_WDT_IRQ_RESET           ESP_WDT_IRQ_RESET

typedef ESP32C3TimgState ESP32P4TimgState;
typedef ESP32C3TimgClass ESP32P4TimgClass;

/*
 * ESP32-C5 System Timer
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_systimer.h"

#define TYPE_ESP32C5_SYSTIMER           "esp32c5.systimer"
#define ESP32C5_SYSTIMER(obj)           OBJECT_CHECK(ESP32C5SysTimerState, (obj), TYPE_ESP32C5_SYSTIMER)
#define ESP32C5_SYSTIMER_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5SysTimerClass, obj, TYPE_ESP32C5_SYSTIMER)
#define ESP32C5_SYSTIMER_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5SysTimerClass, (klass), TYPE_ESP32C5_SYSTIMER)

typedef struct ESP32C5SysTimerState {
    ESP32C3SysTimerState parent;
} ESP32C5SysTimerState;

typedef struct ESP32C5SysTimerClass {
    ESP32C3SysTimerClass parent_class;
} ESP32C5SysTimerClass;

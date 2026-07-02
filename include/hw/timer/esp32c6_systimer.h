/*
 * ESP32-C6 System Timer
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_systimer.h"

#define TYPE_ESP32C6_SYSTIMER           "esp32c6.systimer"
#define ESP32C6_SYSTIMER(obj)           OBJECT_CHECK(ESP32C6SysTimerState, (obj), TYPE_ESP32C6_SYSTIMER)
#define ESP32C6_SYSTIMER_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6SysTimerClass, obj, TYPE_ESP32C6_SYSTIMER)
#define ESP32C6_SYSTIMER_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6SysTimerClass, (klass), TYPE_ESP32C6_SYSTIMER)

typedef struct ESP32C6SysTimerState {
    ESP32C3SysTimerState parent;
} ESP32C6SysTimerState;

typedef struct ESP32C6SysTimerClass {
    ESP32C3SysTimerClass parent_class;
} ESP32C6SysTimerClass;

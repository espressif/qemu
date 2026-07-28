/*
 * ESP32-P4 System Timer
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_systimer.h"

#define TYPE_ESP32P4_SYSTIMER           "esp32p4.systimer"
#define ESP32P4_SYSTIMER(obj)           OBJECT_CHECK(ESP32P4SysTimerState, (obj), TYPE_ESP32P4_SYSTIMER)
#define ESP32P4_SYSTIMER_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32P4SysTimerClass, obj, TYPE_ESP32P4_SYSTIMER)
#define ESP32P4_SYSTIMER_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32P4SysTimerClass, (klass), TYPE_ESP32P4_SYSTIMER)

typedef struct ESP32P4SysTimerState {
    ESP32C3SysTimerState parent;
} ESP32P4SysTimerState;

typedef struct ESP32P4SysTimerClass {
    ESP32C3SysTimerClass parent_class;
} ESP32P4SysTimerClass;

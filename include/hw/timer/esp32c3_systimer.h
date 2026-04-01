/*
 * ESP32-C3 System Timer emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/timer/esp_systimer.h"

#define TYPE_ESP32C3_SYSTIMER           "esp32c3.systimer"
#define ESP32C3_SYSTIMER(obj)           OBJECT_CHECK(ESP32C3SysTimerState, (obj), TYPE_ESP32C3_SYSTIMER)
#define ESP32C3_SYSTIMER_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3SysTimerClass, obj, TYPE_ESP32C3_SYSTIMER)
#define ESP32C3_SYSTIMER_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C3SysTimerClass, (klass), TYPE_ESP32C3_SYSTIMER)


typedef struct ESP32C3SysTimerState {
    ESPSysTimerState parent;
} ESP32C3SysTimerState;


typedef struct ESP32C3SysTimerClass {
    ESPSysTimerClass parent_class;
    /* Virtual attributes/methods overriden */
    uint64_t (*parent_systimer_read)(void *opaque, hwaddr addr, unsigned int size);
    void (*parent_systimer_write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
} ESP32C3SysTimerClass;

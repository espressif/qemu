/*
 * ESP32-H4 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/riscv/esp32c3_clk.h"

#define TYPE_ESP32H4_CLOCK "esp32h4.soc.clk"
#define ESP32H4_CLOCK(obj)           OBJECT_CHECK(ESP32H4ClockState, (obj), TYPE_ESP32H4_CLOCK)
#define ESP32H4_CLOCK_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4ClockClass, obj, TYPE_ESP32H4_CLOCK)
#define ESP32H4_CLOCK_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4ClockClass, klass, TYPE_ESP32H4_CLOCK)

typedef struct ESP32H4ClockState {
    ESP32C3ClockState parent;
} ESP32H4ClockState;

typedef struct ESP32H4ClockClass {
    ESP32C3ClockClass parent_class;
} ESP32H4ClockClass;

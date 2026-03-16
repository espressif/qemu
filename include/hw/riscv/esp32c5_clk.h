/*
 * ESP32-C5 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/riscv/esp32c3_clk.h"

#define TYPE_ESP32C5_CLOCK "esp32c5.soc.clk"
#define ESP32C5_CLOCK(obj)           OBJECT_CHECK(ESP32C5ClockState, (obj), TYPE_ESP32C5_CLOCK)
#define ESP32C5_CLOCK_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5ClockClass, obj, TYPE_ESP32C5_CLOCK)
#define ESP32C5_CLOCK_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5ClockClass, klass, TYPE_ESP32C5_CLOCK)

typedef struct ESP32C5ClockState {
    ESP32C3ClockState parent;
} ESP32C5ClockState;

typedef struct ESP32C5ClockClass {
    ESP32C3ClockClass parent_class;
} ESP32C5ClockClass;

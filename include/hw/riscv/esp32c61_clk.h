/*
 * ESP32-C61 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/riscv/esp32c3_clk.h"

#define TYPE_ESP32C61_CLOCK "esp32c61.soc.clk"
#define ESP32C61_CLOCK(obj)           OBJECT_CHECK(ESP32C61ClockState, (obj), TYPE_ESP32C61_CLOCK)
#define ESP32C61_CLOCK_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C61ClockClass, obj, TYPE_ESP32C61_CLOCK)
#define ESP32C61_CLOCK_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C61ClockClass, klass, TYPE_ESP32C61_CLOCK)

typedef struct ESP32C61ClockState {
    ESP32C3ClockState parent;
} ESP32C61ClockState;

typedef struct ESP32C61ClockClass {
    ESP32C3ClockClass parent_class;
} ESP32C61ClockClass;

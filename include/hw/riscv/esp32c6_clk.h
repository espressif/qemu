/*
 * ESP32-C6 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/riscv/esp32c3_clk.h"

#define TYPE_ESP32C6_CLOCK "esp32c6.soc.clk"
#define ESP32C6_CLOCK(obj)           OBJECT_CHECK(ESP32C6ClockState, (obj), TYPE_ESP32C6_CLOCK)
#define ESP32C6_CLOCK_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6ClockClass, obj, TYPE_ESP32C6_CLOCK)
#define ESP32C6_CLOCK_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6ClockClass, klass, TYPE_ESP32C6_CLOCK)

typedef struct ESP32C6ClockState {
    ESP32C3ClockState parent;
} ESP32C6ClockState;

typedef struct ESP32C6ClockClass {
    ESP32C3ClockClass parent_class;
} ESP32C6ClockClass;

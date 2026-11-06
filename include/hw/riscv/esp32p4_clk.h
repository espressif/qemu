/*
 * ESP32-P4 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/riscv/esp32c3_clk.h"

#define TYPE_ESP32P4_CLOCK "esp32p4.soc.clk"
#define ESP32P4_CLOCK(obj)           OBJECT_CHECK(ESP32P4ClockState, (obj), TYPE_ESP32P4_CLOCK)
#define ESP32P4_CLOCK_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32P4ClockClass, obj, TYPE_ESP32P4_CLOCK)
#define ESP32P4_CLOCK_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32P4ClockClass, klass, TYPE_ESP32P4_CLOCK)

typedef struct ESP32P4ClockState {
    ESP32C3ClockState parent;
} ESP32P4ClockState;

typedef struct ESP32P4ClockClass {
    ESP32C3ClockClass parent_class;
} ESP32P4ClockClass;

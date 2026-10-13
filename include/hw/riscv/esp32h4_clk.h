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

#include "hw/resettable.h"
#include "hw/riscv/esp32c3_clk.h"

#define TYPE_ESP32H4_CLOCK "esp32h4.soc.clk"
#define ESP32H4_CLOCK(obj)           OBJECT_CHECK(ESP32H4ClockState, (obj), TYPE_ESP32H4_CLOCK)
#define ESP32H4_CLOCK_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32H4ClockClass, obj, TYPE_ESP32H4_CLOCK)
#define ESP32H4_CLOCK_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32H4ClockClass, klass, TYPE_ESP32H4_CLOCK)

typedef struct ESP32H4ClockState {
    ESP32C3ClockState parent;
    /*
     * HP_SYSTEM_APPCPU_BOOT_ADDR at +0x84. The C3 clock window already covers
     * this offset and drops the access, so a 4-byte overlay is mapped on top.
     * The ROM's core1 start-up loop spins on this word and jumps to it once
     * core0 makes it non-zero.
     */
    MemoryRegion boot_addr_mr;
    uint32_t core1_boot_addr;
} ESP32H4ClockState;

typedef struct ESP32H4ClockClass {
    ESP32C3ClockClass parent_class;
    ResettablePhases parent_phases;
} ESP32H4ClockClass;

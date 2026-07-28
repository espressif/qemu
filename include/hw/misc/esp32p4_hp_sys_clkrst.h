/*
 * ESP32-P4 HP_SYS_CLKRST
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32P4_HP_SYS_CLKRST "esp32p4.hp_sys_clkrst"
#define ESP32P4_HP_SYS_CLKRST(obj) \
    OBJECT_CHECK(ESP32P4HpSysClkrstState, (obj), TYPE_ESP32P4_HP_SYS_CLKRST)

#define ESP32P4_HP_SYS_CLKRST_IO_SIZE 0x1000

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t reg[ESP32P4_HP_SYS_CLKRST_IO_SIZE / 4];
} ESP32P4HpSysClkrstState;

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

/*
 * Raised while HP core1 is clocked and out of reset, i.e. once the guest has
 * set SOC_CLK_CTRL0.CORE1_CPU_CLK_EN and cleared HP_RST_EN0.RST_EN_CORE1_GLOBAL.
 */
#define ESP32P4_HP_SYS_CLKRST_CORE1_RUN_GPIO "esp32p4.hp_sys_clkrst.core1-run"

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t reg[ESP32P4_HP_SYS_CLKRST_IO_SIZE / 4];
    qemu_irq core1_run;
} ESP32P4HpSysClkrstState;

/*
 * ESP32-P4 HP_SYSTEM — software-triggered CPU interrupts
 *
 * CPU_INT_FROM_CPU_0..3 live in HP_SYSTEM (not INTPRI) at offsets 0x10..0x1c.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32P4_HP_SYSTEM "esp32p4.hp_system"
#define ESP32P4_HP_SYSTEM(obj) \
    OBJECT_CHECK(ESP32P4HpSystemState, (obj), TYPE_ESP32P4_HP_SYSTEM)

#define ESP32P4_HP_SYSTEM_CPU_INTR_COUNT 4
#define ESP32P4_HP_SYSTEM_IO_SIZE        0x100
#define ESP32P4_HP_SYSTEM_FROM_CPU_OFF   0x10

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    qemu_irq irqs[ESP32P4_HP_SYSTEM_CPU_INTR_COUNT];
    uint32_t levels;
} ESP32P4HpSystemState;

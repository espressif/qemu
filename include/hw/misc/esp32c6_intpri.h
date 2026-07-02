/*
 * ESP32-C6 INTPRI — software-triggered cross-core interrupts
 *
 * On the ESP32-C6 the CPU_INTR_FROM_CPU_x registers live in the INTPRI
 * peripheral block (DR_REG_INTPRI_BASE + 0x90..0x9C) rather than in the
 * SYSTEM peripheral (as on the C3).
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32C6_INTPRI "esp32c6.intpri"
#define ESP32C6_INTPRI(obj) OBJECT_CHECK(ESP32C6IntpriState, (obj), TYPE_ESP32C6_INTPRI)

#define ESP32C6_INTPRI_CPU_INTR_COUNT 4
#define ESP32C6_INTPRI_IO_SIZE        0x100

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    qemu_irq irqs[ESP32C6_INTPRI_CPU_INTR_COUNT];
    uint32_t levels;
} ESP32C6IntpriState;

/*
 * ESP32-C5 INTPRI — software-triggered cross-core interrupts
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32C5_INTPRI "esp32c5.intpri"
#define ESP32C5_INTPRI(obj) OBJECT_CHECK(ESP32C5IntpriState, (obj), TYPE_ESP32C5_INTPRI)

#define ESP32C5_INTPRI_CPU_INTR_COUNT 4
#define ESP32C5_INTPRI_IO_SIZE        0x100

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    qemu_irq irqs[ESP32C5_INTPRI_CPU_INTR_COUNT];
    uint32_t levels;
} ESP32C5IntpriState;

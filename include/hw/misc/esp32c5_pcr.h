/*
 * ESP32-C5 PCR (Peripheral Clock and Reset) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32C5_PCR "esp32c5.pcr"
#define ESP32C5_PCR(obj) OBJECT_CHECK(ESP32C5PcrState, (obj), TYPE_ESP32C5_PCR)

#define ESP32C5_PCR_IO_SIZE 0x1000

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint32_t reg[ESP32C5_PCR_IO_SIZE / 4];
    /* Called with the CPU frequency in Hz at reset and when the guest changes it */
    void (*cpu_freq_changed)(void *opaque, uint32_t freq_hz);
    void *cpu_freq_opaque;
} ESP32C5PcrState;

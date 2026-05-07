/*
 * ESP32-C61 PCR (Peripheral Clock and Reset) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"
#include "hw/misc/esp32c5_pcr.h"

#define TYPE_ESP32C61_PCR "esp32c61.pcr"
#define ESP32C61_PCR(obj) OBJECT_CHECK(ESP32C61PcrState, (obj), TYPE_ESP32C61_PCR)

#define ESP32C61_PCR_IO_SIZE ESP32C5_PCR_IO_SIZE

typedef struct ESP32C61PcrState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t reg[ESP32C61_PCR_IO_SIZE / 4];
    /* Called with the CPU frequency in Hz at reset and when the guest changes it */
    void (*cpu_freq_changed)(void *opaque, uint32_t freq_hz);
    void *cpu_freq_opaque;
} ESP32C61PcrState;

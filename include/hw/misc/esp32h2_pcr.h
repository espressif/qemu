/*
 * ESP32-H2 PCR (Peripheral Clock and Reset) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32H2_PCR "esp32h2.pcr"
#define ESP32H2_PCR(obj) OBJECT_CHECK(ESP32H2PcrState, (obj), TYPE_ESP32H2_PCR)

#define ESP32H2_PCR_IO_SIZE   0x400
#define ESP32H2_PCR_REG_COUNT (ESP32H2_PCR_IO_SIZE / 4)

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint32_t regs[ESP32H2_PCR_REG_COUNT];
} ESP32H2PcrState;

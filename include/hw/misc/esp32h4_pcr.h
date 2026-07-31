/*
 * ESP32-H4 PCR (Peripheral Clock and Reset) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32H4_PCR "esp32h4.pcr"
#define ESP32H4_PCR(obj) OBJECT_CHECK(ESP32H4PcrState, (obj), TYPE_ESP32H4_PCR)

#define ESP32H4_PCR_IO_SIZE 0x1000

typedef struct ESP32H4PcrState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t reg[ESP32H4_PCR_IO_SIZE / 4];
} ESP32H4PcrState;

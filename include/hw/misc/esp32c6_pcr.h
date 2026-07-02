/*
 * ESP32-C6 PCR (Peripheral Clock and Reset) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32C6_PCR "esp32c6.pcr"
#define ESP32C6_PCR(obj) OBJECT_CHECK(ESP32C6PcrState, (obj), TYPE_ESP32C6_PCR)

#define ESP32C6_PCR_IO_SIZE 0x1000

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
} ESP32C6PcrState;

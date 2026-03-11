/*
 * ESP32-H2 I2C Analog Master controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32H2_I2C_ANA_MST "esp32h2.i2c_ana_mst"
#define ESP32H2_I2C_ANA_MST(obj) OBJECT_CHECK(ESP32H2I2cAnaMstState, (obj), TYPE_ESP32H2_I2C_ANA_MST)

#define ESP32H2_I2C_ANA_MST_IO_SIZE 0x100
#define ESP32H2_I2C_ANA_MST_BASE    0x600AD800

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint32_t i2c0_ctrl;
    uint32_t i2c1_ctrl;
    uint8_t regs[256][16];
} ESP32H2I2cAnaMstState;

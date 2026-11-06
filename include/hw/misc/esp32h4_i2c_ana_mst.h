/*
 * ESP32-H4 I2C Analog Master
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_i2c_ana_mst.h"

#define TYPE_ESP32H4_I2C_ANA_MST "esp32h4.i2c_ana_mst"
#define ESP32H4_I2C_ANA_MST(obj) OBJECT_CHECK(ESP32H4I2cAnaMstState, (obj), TYPE_ESP32H4_I2C_ANA_MST)

#define ESP32H4_I2C_ANA_MST_BASE    0x600CF800

typedef struct ESP32H4I2cAnaMstState {
    ESP32C5I2cAnaMstState parent;
} ESP32H4I2cAnaMstState;

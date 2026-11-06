/*
 * ESP32-C61 I2C Analog Master controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_i2c_ana_mst.h"

#define TYPE_ESP32C61_I2C_ANA_MST "esp32c61.i2c_ana_mst"
#define ESP32C61_I2C_ANA_MST(obj) OBJECT_CHECK(ESP32C61I2cAnaMstState, (obj), TYPE_ESP32C61_I2C_ANA_MST)

#define ESP32C61_I2C_ANA_MST_BASE    ESP32C5_I2C_ANA_MST_BASE

typedef struct ESP32C61I2cAnaMstState {
    ESP32C5I2cAnaMstState parent;
} ESP32C61I2cAnaMstState;

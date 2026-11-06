/*
 * ESP32-C61 I2C Analog Master controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32c61_i2c_ana_mst.h"

static const TypeInfo esp32c61_i2c_ana_mst_info = {
    .name = TYPE_ESP32C61_I2C_ANA_MST,
    .parent = TYPE_ESP32C5_I2C_ANA_MST,
    .instance_size = sizeof(ESP32C61I2cAnaMstState),
};

static void esp32c61_i2c_ana_mst_register_types(void)
{
    type_register_static(&esp32c61_i2c_ana_mst_info);
}

type_init(esp32c61_i2c_ana_mst_register_types)

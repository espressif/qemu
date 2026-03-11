/*
 * ESP32-H2 SPI emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/ssi/esp32h2_spi.h"

static const TypeInfo esp32h2_spi_info = {
    .name = TYPE_ESP32H2_SPI,
    .parent = TYPE_ESP32C3_SPI,
    .instance_size = sizeof(ESP32H2SpiState),
};

static void esp32h2_spi_register_types(void)
{
    type_register_static(&esp32h2_spi_info);
}

type_init(esp32h2_spi_register_types)

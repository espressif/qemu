/*
 * ESP32-P4 SPI (SPI1, flash access path)
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/ssi/esp32p4_spi.h"

static const TypeInfo esp32p4_spi_info = {
    .name = TYPE_ESP32P4_SPI,
    .parent = TYPE_ESP32C3_SPI,
    .instance_size = sizeof(ESP32P4SpiState),
};

static void esp32p4_spi_register_types(void)
{
    type_register_static(&esp32p4_spi_info);
}

type_init(esp32p4_spi_register_types)

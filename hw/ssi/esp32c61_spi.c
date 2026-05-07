/*
 * ESP32-C61 SPI
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/ssi/esp32c61_spi.h"

static void esp32c61_spi_init(Object *obj)
{
}

static const TypeInfo esp32c61_spi_info = {
    .name = TYPE_ESP32C61_SPI,
    .parent = TYPE_ESP32C3_SPI,
    .instance_size = sizeof(ESP32C61SpiState),
    .instance_init = esp32c61_spi_init,
};

static void esp32c61_spi_register_types(void)
{
    type_register_static(&esp32c61_spi_info);
}

type_init(esp32c61_spi_register_types)

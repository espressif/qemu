/*
 * ESP32-C6 SPI
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/ssi/esp32c6_spi.h"

static void esp32c6_spi_init(Object *obj)
{
}

static const TypeInfo esp32c6_spi_info = {
    .name = TYPE_ESP32C6_SPI,
    .parent = TYPE_ESP32C3_SPI,
    .instance_size = sizeof(ESP32C6SpiState),
    .instance_init = esp32c6_spi_init,
};

static void esp32c6_spi_register_types(void)
{
    type_register_static(&esp32c6_spi_info);
}

type_init(esp32c6_spi_register_types)

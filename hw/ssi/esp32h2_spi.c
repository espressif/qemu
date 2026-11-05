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

static void esp32h2_spi_init(Object *obj)
{
    ESP32H2SpiState *s = ESP32H2_SPI(obj);

    s->parent.addr_phase_has_wb_mode = true;
}

static const TypeInfo esp32h2_spi_info = {
    .name = TYPE_ESP32H2_SPI,
    .parent = TYPE_ESP32C3_SPI,
    .instance_size = sizeof(ESP32H2SpiState),
    .instance_init = esp32h2_spi_init,
};

static void esp32h2_spi_register_types(void)
{
    type_register_static(&esp32h2_spi_info);
}

type_init(esp32h2_spi_register_types)

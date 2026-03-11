/*
 * ESP32-H2 MODEM_LPCON (Modem Low-Power Controller)
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32H2_MODEM "esp32h2.modem_lpcon"
#define ESP32H2_MODEM(obj) OBJECT_CHECK(ESP32H2ModemState, (obj), TYPE_ESP32H2_MODEM)

#define ESP32H2_MODEM_IO_SIZE 0x100
#define ESP32H2_MODEM_BASE    0x600AD000

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint32_t clk_conf;
    uint32_t clk_conf_force_on;
    uint32_t rst_conf;
} ESP32H2ModemState;

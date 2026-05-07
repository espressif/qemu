/*
 * ESP32-C61 MODEM_LPCON (Modem Low-Power Controller)
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_modem.h"

#define TYPE_ESP32C61_MODEM "esp32c61.modem_lpcon"
#define ESP32C61_MODEM(obj) OBJECT_CHECK(ESP32C61ModemState, (obj), TYPE_ESP32C61_MODEM)

#define ESP32C61_MODEM_IO_SIZE ESP32C5_MODEM_IO_SIZE
#define ESP32C61_MODEM_BASE    ESP32C5_MODEM_BASE

typedef struct ESP32C61ModemState {
    ESP32C5ModemState parent;
} ESP32C61ModemState;

/*
 * ESP32-H4 Modem clock control
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_modem.h"

#define TYPE_ESP32H4_MODEM "esp32h4.modem"
#define ESP32H4_MODEM(obj) OBJECT_CHECK(ESP32H4ModemState, (obj), TYPE_ESP32H4_MODEM)

#define ESP32H4_MODEM_BASE    0x600CF000

typedef struct ESP32H4ModemState {
    ESP32C5ModemState parent;
} ESP32H4ModemState;

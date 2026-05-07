/*
 * ESP32-C61 USB Serial JTAG emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_jtag.h"

#define TYPE_ESP32C61_JTAG "misc.esp32c61.usb_serial_jtag"
#define ESP32C61_JTAG(obj) OBJECT_CHECK(ESP32C61UsbJtagState, (obj), TYPE_ESP32C61_JTAG)

typedef struct ESP32C61UsbJtagState {
    ESP32C5UsbJtagState parent;
} ESP32C61UsbJtagState;

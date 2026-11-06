/*
 * ESP32-H4 USB Serial JTAG emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_jtag.h"

#define TYPE_ESP32H4_JTAG "misc.esp32h4.usb_serial_jtag"
#define ESP32H4_JTAG(obj) OBJECT_CHECK(ESP32H4UsbJtagState, (obj), TYPE_ESP32H4_JTAG)

typedef struct ESP32H4UsbJtagState {
    ESP32C5UsbJtagState parent;
} ESP32H4UsbJtagState;

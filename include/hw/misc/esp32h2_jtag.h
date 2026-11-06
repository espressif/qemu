/*
 * ESP32-H2 USB Serial JTAG emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c6_jtag.h"

/*
 * The ESP32-H2 USB Serial/JTAG controller is architecturally identical to
 * the ESP32-C6's: same register layout, same CDC-ACM + JTAG functionality.
 * H2 is registered as a QOM subtype of the C6 device.
 */

#define TYPE_ESP32H2_JTAG "misc.esp32h2.usb_serial_jtag"
#define ESP32H2_JTAG(obj) OBJECT_CHECK(ESP32C6UsbJtagState, (obj), TYPE_ESP32H2_JTAG)

typedef ESP32C6UsbJtagState ESP32H2UsbJtagState;

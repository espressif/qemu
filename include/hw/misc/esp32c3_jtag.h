/*
 * ESP32-C3 USB Serial JTAG emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"

#define TYPE_ESP32C3_JTAG "misc.esp32c3.usb_serial_jtag"
#define ESP32C3_JTAG(obj) OBJECT_CHECK(ESP32C3UsbJtagState, (obj), TYPE_ESP32C3_JTAG)

#define ESP32C3_JTAG_REGS_SIZE (0x84)


typedef struct ESP32C3UsbJtagState {
    SysBusDevice parent_object;
    MemoryRegion iomem;
} ESP32C3UsbJtagState;


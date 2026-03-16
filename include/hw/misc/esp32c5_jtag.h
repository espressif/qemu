/*
 * ESP32-C5 USB Serial JTAG emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "chardev/char-fe.h"

#define TYPE_ESP32C5_JTAG "misc.esp32c5.usb_serial_jtag"
#define ESP32C5_JTAG(obj) OBJECT_CHECK(ESP32C5UsbJtagState, (obj), TYPE_ESP32C5_JTAG)

#define ESP32C5_JTAG_REGS_SIZE (0x84)

#define USB_SERIAL_JTAG_EP1_REG                  0x00
#define USB_SERIAL_JTAG_EP1_CONF_REG             0x04
#define USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE   (1 << 1)
#define USB_SERIAL_JTAG_WR_DONE                  (1 << 0)

typedef struct ESP32C5UsbJtagState {
    SysBusDevice parent_object;
    MemoryRegion iomem;
    CharBackend chr;
} ESP32C5UsbJtagState;

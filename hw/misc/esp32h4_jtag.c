/*
 * ESP32-H4 USB Serial JTAG emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32h4_jtag.h"

static const TypeInfo esp32h4_jtag_info = {
    .name = TYPE_ESP32H4_JTAG,
    .parent = TYPE_ESP32C5_JTAG,
    .instance_size = sizeof(ESP32H4UsbJtagState),
};

static void esp32h4_jtag_register_types(void)
{
    type_register_static(&esp32h4_jtag_info);
}

type_init(esp32h4_jtag_register_types)

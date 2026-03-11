/*
 * ESP32-H2 USB Serial JTAG emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/*
 * The ESP32-H2 USB Serial/JTAG controller shares the same register layout
 * and behavior as the ESP32-C6. This file registers an H2-specific QOM type
 * that inherits all functionality from the C6 implementation.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32h2_jtag.h"

static const TypeInfo esp32h2_jtag_info = {
    .name = TYPE_ESP32H2_JTAG,
    .parent = TYPE_ESP32C6_JTAG,
};

static void esp32h2_jtag_register_types(void)
{
    type_register_static(&esp32h2_jtag_info);
}

type_init(esp32h2_jtag_register_types)

/*
 * ESP32-H4 MODEM_LPCON (Modem Low-Power Controller)
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32h4_modem.h"

static const TypeInfo esp32h4_modem_info = {
    .name = TYPE_ESP32H4_MODEM,
    .parent = TYPE_ESP32C5_MODEM,
    .instance_size = sizeof(ESP32H4ModemState),
};

static void esp32h4_modem_register_types(void)
{
    type_register_static(&esp32h4_modem_info);
}

type_init(esp32h4_modem_register_types)

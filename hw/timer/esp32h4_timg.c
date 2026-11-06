/*
 * ESP32-H4 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/timer/esp32h4_timg.h"

/*
 * H4 TIMG has only T0, but INT_{ENA,RAW,ST,CLR} still place WDT at bit 2
 * (bit 1 is reserved). Reuse the T0+T1 shift layout so MWDT INT_ENA works.
 */
static void esp32h4_timg_class_init(ObjectClass *klass, void *data)
{
    ESPTimgClass *esp = ESP_TIMG_CLASS(klass);
    esp->m_has_t1 = true;
}

static const TypeInfo esp32h4_timg_info = {
    .name = TYPE_ESP32H4_TIMG,
    .parent = TYPE_ESP32C3_TIMG,
    .instance_size = sizeof(ESP32H4TimgState),
    .class_size = sizeof(ESP32H4TimgClass),
    .class_init = esp32h4_timg_class_init,
};

static void esp32h4_timg_register_types(void)
{
    type_register_static(&esp32h4_timg_info);
}

type_init(esp32h4_timg_register_types)

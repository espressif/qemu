/*
 * ESP32-C5 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/dma/esp32c5_gdma.h"

static void esp32c5_gdma_class_init(ObjectClass *klass, void *data)
{
    ESP_GDMA_CLASS(klass)->m_channel_count = ESP32C5_GDMA_CHANNEL_COUNT;
}

static const TypeInfo esp32c5_gdma_info = {
    .name = TYPE_ESP32C5_GDMA,
    .parent = TYPE_ESP_AHB_GDMA,
    .instance_size = sizeof(ESP32C5GdmaState),
    .class_init = esp32c5_gdma_class_init,
    .class_size = sizeof(ESP32C5GdmaClass),
};

static void esp32c5_gdma_register_types(void)
{
    type_register_static(&esp32c5_gdma_info);
}

type_init(esp32c5_gdma_register_types)

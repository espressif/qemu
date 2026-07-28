/*
 * ESP32-P4 SHA accelerator
 *
 * The register layout is identical to the shared esp_sha model, verified
 * against IDF components/soc/esp32p4/register/hw_ver3/soc/sha_reg.h, so this
 * is a thin wrapper over TYPE_ESP32C3_SHA. Only SHA_DATE and the M_MEM width
 * differ.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/misc/esp32p4_sha.h"

static void esp32p4_sha_realize(DeviceState *dev, Error **errp)
{
    /*
     * The P4 machine has no GDMA model yet; SHA is only reachable in CPU
     * (M_MEM) mode, so skip the parent's mandatory GDMA link check.
     */
}

static void esp32p4_sha_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESPShaClass *class = ESP_SHA_CLASS(klass);

    dc->realize = esp32p4_sha_realize;
    class->message_len = ESP32P4_SHA_MESSAGE_SIZE;
    class->date = ESP32P4_SHA_DATE_REG_VALUE;
}

static const TypeInfo esp32p4_sha_info = {
    .name = TYPE_ESP32P4_SHA,
    .parent = TYPE_ESP32C3_SHA,
    .instance_size = sizeof(ESP32P4ShaState),
    .class_init = esp32p4_sha_class_init,
    .class_size = sizeof(ESP32P4ShaClass),
};

static void esp32p4_sha_register_types(void)
{
    type_register_static(&esp32p4_sha_info);
}

type_init(esp32p4_sha_register_types)

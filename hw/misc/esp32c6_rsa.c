/*
 * RSA accelerator of ESP32-C6, also used by ESP32-C5 and ESP32-H2, and of ESP32-P4
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/misc/esp32c6_rsa.h"

static void esp32c6_rsa_class_init(ObjectClass *klass, void *data)
{
    ESPRsaClass *class = ESP_RSA_CLASS(klass);

    class->rsa_mem_blk_size = ESP32C6_RSA_MEM_BLK_SIZE;
    class->date = ESP32C6_RSA_DATE_REG_VALUE;
}

static void esp32p4_rsa_class_init(ObjectClass *klass, void *data)
{
    ESPRsaClass *class = ESP_RSA_CLASS(klass);

    class->rsa_mem_blk_size = ESP32P4_RSA_MEM_BLK_SIZE;
    class->date = ESP32C6_RSA_DATE_REG_VALUE;
}

static const TypeInfo esp32c6_rsa_types[] = {
    {
        .name = TYPE_ESP32C6_RSA,
        .parent = TYPE_ESP_RSA,
        .instance_size = sizeof(ESP32C6RsaState),
        .class_init = esp32c6_rsa_class_init,
        .class_size = sizeof(ESP32C6RsaClass),
    },
    {
        .name = TYPE_ESP32P4_RSA,
        .parent = TYPE_ESP_RSA,
        .instance_size = sizeof(ESP32C6RsaState),
        .class_init = esp32p4_rsa_class_init,
        .class_size = sizeof(ESP32C6RsaClass),
    },
};

DEFINE_TYPES(esp32c6_rsa_types)

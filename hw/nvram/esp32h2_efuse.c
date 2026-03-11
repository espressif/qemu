/*
 * ESP32-H2 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/nvram/esp32h2_efuse.h"


static void esp32h2_efuse_realize(DeviceState *dev, Error **errp)
{
    ESP32H2EfuseClass *h2_class = ESP32H2_EFUSE_GET_CLASS(dev);
    ESPEfuseState *s = ESP_EFUSE(dev);

    h2_class->parent_realize(dev, errp);

    if (s->blk == NULL) {
        assert(s->mirror != NULL);

        /*
         * Chip revision v0.0 and eFuse block revision v0.0, as read from a real
         * chip. The parent wrote the C3 values, in fields that are at other
         * positions on H2.
         */
        s->efuses.blocks.rd_mac_spi_sys_3 = 0;
        s->efuses.blocks.rd_sys_part1_data4 = 0;

        memcpy(s->mirror, &s->efuses.blocks, sizeof(ESPEfuseBlocks));
    }
}


static void esp32h2_efuse_init(Object *obj)
{
}

static void esp32h2_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32H2EfuseClass *h2_efuse = ESP32H2_EFUSE_CLASS(klass);

    device_class_set_parent_realize(dc, esp32h2_efuse_realize,
                                    &h2_efuse->parent_realize);
}

static const TypeInfo esp32h2_efuse_info = {
    .name = TYPE_ESP32H2_EFUSE,
    .parent = TYPE_ESP32C3_EFUSE,
    .instance_size = sizeof(ESP32H2EfuseState),
    .instance_init = esp32h2_efuse_init,
    .class_init = esp32h2_efuse_class_init,
    .class_size = sizeof(ESP32H2EfuseClass),
};

static void esp32h2_efuse_register_types(void)
{
    type_register_static(&esp32h2_efuse_info);
}

type_init(esp32h2_efuse_register_types)

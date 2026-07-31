/*
 * ESP32-H4 eFuse emulation
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
#include "hw/nvram/esp32h4_efuse.h"

static void esp32h4_efuse_realize(DeviceState *dev, Error **errp)
{
    ESP32H4EfuseClass *h4_class = ESP32H4_EFUSE_GET_CLASS(dev);
    ESPEfuseState *s = ESP_EFUSE(dev);

    h4_class->parent_realize(dev, errp);

    if (s->blk == NULL) {
        assert(s->mirror != NULL);

        s->efuses.blocks.rd_mac_spi_sys_3 =
            FIELD_DP32(0, EFUSE_H4_RD_MAC_SYS_3, WAFER_VER_MINOR, 2);
        s->efuses.blocks.rd_mac_spi_sys_3 =
            FIELD_DP32(s->efuses.blocks.rd_mac_spi_sys_3,
                       EFUSE_H4_RD_MAC_SYS_3, WAFER_VER_MAJOR, 0);

        s->efuses_internal.blocks.rd_mac_spi_sys_3 =
            s->efuses.blocks.rd_mac_spi_sys_3;

        memcpy(s->mirror, &s->efuses.blocks, sizeof(ESPEfuseBlocks));
    }
}

static void esp32h4_efuse_init(Object *obj)
{
}

static void esp32h4_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32H4EfuseClass *h4_efuse = ESP32H4_EFUSE_CLASS(klass);

    device_class_set_parent_realize(dc, esp32h4_efuse_realize,
                                    &h4_efuse->parent_realize);
}

static const TypeInfo esp32h4_efuse_info = {
    .name = TYPE_ESP32H4_EFUSE,
    .parent = TYPE_ESP32C3_EFUSE,
    .instance_size = sizeof(ESP32H4EfuseState),
    .instance_init = esp32h4_efuse_init,
    .class_init = esp32h4_efuse_class_init,
    .class_size = sizeof(ESP32H4EfuseClass),
};

static void esp32h4_efuse_register_types(void)
{
    type_register_static(&esp32h4_efuse_info);
}

type_init(esp32h4_efuse_register_types)

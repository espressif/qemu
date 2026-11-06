/*
 * ESP32-C61 eFuse emulation
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
#include "hw/nvram/esp32c61_efuse.h"

static void esp32c61_efuse_realize(DeviceState *dev, Error **errp)
{
    ESP32C61EfuseClass *c61_class = ESP32C61_EFUSE_GET_CLASS(dev);
    ESPEfuseState *s = ESP_EFUSE(dev);

    c61_class->parent_realize(dev, errp);

    if (s->blk == NULL) {
        assert(s->mirror != NULL);

        /* Values of a current chip: revision v1.2, eFuse block revision v0.1 */
        s->efuses.blocks.rd_mac_spi_sys_2 =
            FIELD_DP32(0, EFUSE_C61_RD_MAC_SYS_2, WAFER_VER_MINOR, 2);
        s->efuses.blocks.rd_mac_spi_sys_2 =
            FIELD_DP32(s->efuses.blocks.rd_mac_spi_sys_2,
                       EFUSE_C61_RD_MAC_SYS_2, WAFER_VER_MAJOR, 1);
        s->efuses.blocks.rd_mac_spi_sys_2 =
            FIELD_DP32(s->efuses.blocks.rd_mac_spi_sys_2,
                       EFUSE_C61_RD_MAC_SYS_2, BLK_VER_MINOR, 1);

        s->efuses_internal.blocks.rd_mac_spi_sys_2 =
            s->efuses.blocks.rd_mac_spi_sys_2;

        memcpy(s->mirror, &s->efuses.blocks, sizeof(ESPEfuseBlocks));
    }
}

static void esp32c61_efuse_init(Object *obj)
{
}

static void esp32c61_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32C61EfuseClass *c61_efuse = ESP32C61_EFUSE_CLASS(klass);

    device_class_set_parent_realize(dc, esp32c61_efuse_realize,
                                    &c61_efuse->parent_realize);
}

static const TypeInfo esp32c61_efuse_info = {
    .name = TYPE_ESP32C61_EFUSE,
    .parent = TYPE_ESP32C3_EFUSE,
    .instance_size = sizeof(ESP32C61EfuseState),
    .instance_init = esp32c61_efuse_init,
    .class_init = esp32c61_efuse_class_init,
    .class_size = sizeof(ESP32C61EfuseClass),
};

static void esp32c61_efuse_register_types(void)
{
    type_register_static(&esp32c61_efuse_info);
}

type_init(esp32c61_efuse_register_types)

/*
 * ESP32-P4 eFuse emulation
 *
 * The block read registers (BLK0..BLK10, 0x2C..0x178) and the control
 * registers up to EFUSE_RD_TIM_CONF (0x1EC) match the shared esp_efuse model,
 * verified against IDF components/soc/esp32p4/register/hw_ver3/soc/efuse_reg.h.
 * The write-timing registers above that are shifted by one word on the P4, but
 * they are configuration-only and never gate boot, so this stays a wrapper.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/nvram/esp32p4_efuse.h"

static void esp32p4_efuse_realize(DeviceState *dev, Error **errp)
{
    ESP32P4EfuseClass *p4_class = ESP32P4_EFUSE_GET_CLASS(dev);
    ESPEfuseState *s = ESP_EFUSE(dev);

    p4_class->parent_realize(dev, errp);

    if (s->blk == NULL) {
        assert(s->mirror != NULL);

        const uint32_t major = ESP32P4_EFUSE_WAFER_VERSION_MAJOR;
        uint32_t mac_sys_2 = 0;

        mac_sys_2 = FIELD_DP32(mac_sys_2, EFUSE_P4_RD_MAC_SYS_2,
                               WAFER_VER_MINOR,
                               ESP32P4_EFUSE_WAFER_VERSION_MINOR);
        mac_sys_2 = FIELD_DP32(mac_sys_2, EFUSE_P4_RD_MAC_SYS_2,
                               WAFER_VER_MAJOR_LO, major & 0x3);
        mac_sys_2 = FIELD_DP32(mac_sys_2, EFUSE_P4_RD_MAC_SYS_2,
                               WAFER_VER_MAJOR_HI, (major >> 2) & 0x1);

        s->efuses.blocks.rd_mac_spi_sys_2 = mac_sys_2;
        s->efuses_internal.blocks.rd_mac_spi_sys_2 = mac_sys_2;

        memcpy(s->mirror, &s->efuses.blocks, sizeof(ESPEfuseBlocks));
    }
}

static void esp32p4_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32P4EfuseClass *p4_efuse = ESP32P4_EFUSE_CLASS(klass);

    device_class_set_parent_realize(dc, esp32p4_efuse_realize,
                                    &p4_efuse->parent_realize);
}

static const TypeInfo esp32p4_efuse_info = {
    .name = TYPE_ESP32P4_EFUSE,
    .parent = TYPE_ESP32C3_EFUSE,
    .instance_size = sizeof(ESP32P4EfuseState),
    .class_init = esp32p4_efuse_class_init,
    .class_size = sizeof(ESP32P4EfuseClass),
};

static void esp32p4_efuse_register_types(void)
{
    type_register_static(&esp32p4_efuse_info);
}

type_init(esp32p4_efuse_register_types)

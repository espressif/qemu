/*
 * ESP32-C5 MODEM_LPCON (Modem Low-Power Controller) emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c5_modem.h"

#define CLK_CONF_OFF        0x18
#define CLK_I2C_MST_EN_BIT  (1 << 2)

static uint64_t esp32c5_modem_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5ModemState *s = ESP32C5_MODEM(opaque);
    uint64_t r = 0;

    if (addr == CLK_CONF_OFF) {
        r = s->clk_conf;
    }

    return r;
}

static void esp32c5_modem_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32C5ModemState *s = ESP32C5_MODEM(opaque);

    if (addr == CLK_CONF_OFF) {
        /*
         * Always keep clk_i2c_mst_en set: QEMU has no real clock gating,
         * so the I2C analog master is always available.  The ROM startup
         * code temporarily clears this bit, then asserts it is set inside
         * regi2c_enable_block() — without this the assertion fires.
         */
        s->clk_conf = (uint32_t)value | CLK_I2C_MST_EN_BIT;
    }
}

static const MemoryRegionOps esp32c5_modem_ops = {
    .read  = esp32c5_modem_read,
    .write = esp32c5_modem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c5_modem_realize(DeviceState *dev, Error **errp)
{
    ESP32C5ModemState *s = ESP32C5_MODEM(dev);
    s->clk_conf = 0xFFFFFFFF;
}

static void esp32c5_modem_init(Object *obj)
{
    ESP32C5ModemState *s = ESP32C5_MODEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c5_modem_ops, s,
                          TYPE_ESP32C5_MODEM, ESP32C5_MODEM_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c5_modem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = esp32c5_modem_realize;
}

static const TypeInfo esp32c5_modem_info = {
    .name = TYPE_ESP32C5_MODEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5ModemState),
    .instance_init = esp32c5_modem_init,
    .class_init = esp32c5_modem_class_init,
};

static void esp32c5_modem_register_types(void)
{
    type_register_static(&esp32c5_modem_info);
}

type_init(esp32c5_modem_register_types)

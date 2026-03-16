/*
 * ESP32-C5 I2C Analog Master controller emulation
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
#include "hw/misc/esp32c5_i2c_ana_mst.h"

#define I2C0_CTRL_OFF  0x00
#define I2C1_CTRL_OFF  0x04
#define ANA_CONF0_OFF  0x18

static uint64_t esp32c5_i2c_ana_mst_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5I2cAnaMstState *s = ESP32C5_I2C_ANA_MST(opaque);
    uint64_t r = 0;

    switch (addr) {
    case I2C0_CTRL_OFF: {
        uint8_t block = s->i2c0_ctrl & 0xFF;
        uint8_t reg_addr = (s->i2c0_ctrl >> 8) & 0xFF;
        r = (uint32_t)s->regs[block][reg_addr & 0x0F] << 16;
        break;
    }
    case I2C1_CTRL_OFF: {
        uint8_t block = s->i2c1_ctrl & 0xFF;
        uint8_t reg_addr = (s->i2c1_ctrl >> 8) & 0xFF;
        r = (uint32_t)s->regs[block][reg_addr & 0x0F] << 16;
        break;
    }
    case ANA_CONF0_OFF:
        r = 1 << 24;
        break;
    default:
        break;
    }

    return r;
}

static void esp32c5_i2c_ana_mst_write(void *opaque, hwaddr addr, uint64_t value,
                                       unsigned int size)
{
    ESP32C5I2cAnaMstState *s = ESP32C5_I2C_ANA_MST(opaque);
    uint32_t val32 = (uint32_t)value;

    switch (addr) {
    case I2C0_CTRL_OFF:
        s->i2c0_ctrl = val32;
        if (val32 & (1 << 24)) {
            uint8_t block = val32 & 0xFF;
            uint8_t reg_addr = (val32 >> 8) & 0xFF;
            uint8_t data = (val32 >> 16) & 0xFF;
            s->regs[block][reg_addr & 0x0F] = data;
        }
        break;
    case I2C1_CTRL_OFF:
        s->i2c1_ctrl = val32;
        if (val32 & (1 << 24)) {
            uint8_t block = val32 & 0xFF;
            uint8_t reg_addr = (val32 >> 8) & 0xFF;
            uint8_t data = (val32 >> 16) & 0xFF;
            s->regs[block][reg_addr & 0x0F] = data;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32c5_i2c_ana_mst_ops = {
    .read  = esp32c5_i2c_ana_mst_read,
    .write = esp32c5_i2c_ana_mst_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c5_i2c_ana_mst_init(Object *obj)
{
    ESP32C5I2cAnaMstState *s = ESP32C5_I2C_ANA_MST(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c5_i2c_ana_mst_ops, s,
                          TYPE_ESP32C5_I2C_ANA_MST, ESP32C5_I2C_ANA_MST_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c5_i2c_ana_mst_realize(DeviceState *dev, Error **errp)
{
    ESP32C5I2cAnaMstState *s = ESP32C5_I2C_ANA_MST(dev);

    s->regs[0x61][3] = 0x09;
    s->regs[0x66][8] = 0xC0;
}

static void esp32c5_i2c_ana_mst_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32c5_i2c_ana_mst_realize;
}

static const TypeInfo esp32c5_i2c_ana_mst_info = {
    .name = TYPE_ESP32C5_I2C_ANA_MST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5I2cAnaMstState),
    .instance_init = esp32c5_i2c_ana_mst_init,
    .class_init = esp32c5_i2c_ana_mst_class_init,
};

static void esp32c5_i2c_ana_mst_register_types(void)
{
    type_register_static(&esp32c5_i2c_ana_mst_info);
}

type_init(esp32c5_i2c_ana_mst_register_types)

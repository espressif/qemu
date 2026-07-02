/*
 * ESP32-C6 I2C Analog Master controller emulation
 *
 * The I2C Analog Master is a single-register interface used by IDF/ROM to
 * read and write the analog blocks (BBPLL, RTC LDOs, ...) over an internal
 * I2C bus. A 32-bit transaction word selects a target block, register
 * offset, write data and a write-enable strobe; on read, the target byte
 * is returned in bits [23:16].
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
#include "hw/registerfields.h"
#include "hw/misc/esp32c6_i2c_ana_mst.h"

/* ======================================================================
 * ESP32-C6 MODEM_LPCON / I2C analog master register layout.
 * From ESP-IDF: components/soc/esp32c6/register/soc/modem_lpcon_reg.h.
 * ====================================================================== */

REG32(I2C0_CTRL, 0x00)
    FIELD(I2C0_CTRL, BLOCK,    0,  8)   /* analog block address */
    FIELD(I2C0_CTRL, REG_ADDR, 8,  8)   /* register offset within the block */
    FIELD(I2C0_CTRL, WRDATA,   16, 8)   /* data to write */
    FIELD(I2C0_CTRL, WRITE,    24, 1)   /* 1 = write transaction */
REG32(I2C1_CTRL, 0x04)
    FIELD(I2C1_CTRL, BLOCK,    0,  8)
    FIELD(I2C1_CTRL, REG_ADDR, 8,  8)
    FIELD(I2C1_CTRL, WRDATA,   16, 8)
    FIELD(I2C1_CTRL, WRITE,    24, 1)
REG32(ANA_CONF0, 0x18)
    FIELD(ANA_CONF0, BBPLL_CAL_DONE, 24, 1) /* poll-ready bit returned to IDF */

/* ----------------------------------------------------------------------
 * Helpers operating on a 32-bit I2C transaction word.
 * ---------------------------------------------------------------------- */

static uint32_t i2c_ana_read_value(ESP32C6I2cAnaMstState *s, uint32_t ctrl)
{
    const uint8_t block    = FIELD_EX32(ctrl, I2C0_CTRL, BLOCK);
    const uint8_t reg_addr = FIELD_EX32(ctrl, I2C0_CTRL, REG_ADDR);
    return (uint32_t)s->regs[block][reg_addr & 0x0F]
           << R_I2C0_CTRL_WRDATA_SHIFT;
}

static void i2c_ana_write_value(ESP32C6I2cAnaMstState *s, uint32_t ctrl)
{
    if (!FIELD_EX32(ctrl, I2C0_CTRL, WRITE)) {
        return;
    }
    const uint8_t block    = FIELD_EX32(ctrl, I2C0_CTRL, BLOCK);
    const uint8_t reg_addr = FIELD_EX32(ctrl, I2C0_CTRL, REG_ADDR);
    const uint8_t data     = FIELD_EX32(ctrl, I2C0_CTRL, WRDATA);
    s->regs[block][reg_addr & 0x0F] = data;
}

static uint64_t esp32c6_i2c_ana_mst_read(void *opaque, hwaddr addr,
                                          unsigned int size)
{
    ESP32C6I2cAnaMstState *s = ESP32C6_I2C_ANA_MST(opaque);

    switch (addr) {
    case A_I2C0_CTRL:
        return i2c_ana_read_value(s, s->i2c0_ctrl);

    case A_I2C1_CTRL:
        return i2c_ana_read_value(s, s->i2c1_ctrl);

    case A_ANA_CONF0:
        /* IDF/ROM polls this bit to confirm BBPLL calibration is finished;
         * we don't model the calibration delay so report it as done. */
        return R_ANA_CONF0_BBPLL_CAL_DONE_MASK;

    default:
        return 0;
    }
}

static void esp32c6_i2c_ana_mst_write(void *opaque, hwaddr addr,
                                       uint64_t value, unsigned int size)
{
    ESP32C6I2cAnaMstState *s = ESP32C6_I2C_ANA_MST(opaque);
    const uint32_t val32 = (uint32_t)value;

    switch (addr) {
    case A_I2C0_CTRL:
        s->i2c0_ctrl = val32;
        i2c_ana_write_value(s, val32);
        break;

    case A_I2C1_CTRL:
        s->i2c1_ctrl = val32;
        i2c_ana_write_value(s, val32);
        break;

    default:
        break;
    }
}

static const MemoryRegionOps esp32c6_i2c_ana_mst_ops = {
    .read  = esp32c6_i2c_ana_mst_read,
    .write = esp32c6_i2c_ana_mst_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_i2c_ana_mst_init(Object *obj)
{
    ESP32C6I2cAnaMstState *s = ESP32C6_I2C_ANA_MST(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_i2c_ana_mst_ops, s,
                          TYPE_ESP32C6_I2C_ANA_MST, ESP32C6_I2C_ANA_MST_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c6_i2c_ana_mst_realize(DeviceState *dev, Error **errp)
{
    ESP32C6I2cAnaMstState *s = ESP32C6_I2C_ANA_MST(dev);

    s->regs[0x61][3] = 0x09;
    s->regs[0x66][8] = 0xC0;
}

static void esp32c6_i2c_ana_mst_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32c6_i2c_ana_mst_realize;
}

static const TypeInfo esp32c6_i2c_ana_mst_info = {
    .name = TYPE_ESP32C6_I2C_ANA_MST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6I2cAnaMstState),
    .instance_init = esp32c6_i2c_ana_mst_init,
    .class_init = esp32c6_i2c_ana_mst_class_init,
};

static void esp32c6_i2c_ana_mst_register_types(void)
{
    type_register_static(&esp32c6_i2c_ana_mst_info);
}

type_init(esp32c6_i2c_ana_mst_register_types)

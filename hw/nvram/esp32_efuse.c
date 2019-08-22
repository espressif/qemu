/*
 * ESP32 eFuse emulation
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "chardev/char-fe.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/nvram/esp32_efuse.h"

static void esp32_efuse_read_op(Esp32EfuseState *s);
static void esp32_efuse_program_op(Esp32EfuseState *s);
static void esp32_efuse_update_irq(Esp32EfuseState *s);
static void esp32_efuse_op_timer_start(Esp32EfuseState *s);

static uint64_t esp32_efuse_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32EfuseState *s = ESP32_EFUSE(opaque);
    uint64_t r = 0;
    int idx;
    switch (addr) {
    case A_EFUSE_BLK0_RDATA0 ... A_EFUSE_BLK0_WDATA0 - 4:
        idx = (addr - A_EFUSE_BLK0_RDATA0) / 4;
        r = s->efuse_wr.blk0[idx] & ~(s->efuse_rd_dis.blk0[idx]);
        break;
    case A_EFUSE_BLK1_RDATA0 ... A_EFUSE_BLK2_RDATA0 - 4:
        idx = (addr - A_EFUSE_BLK1_RDATA0) / 4;
        r = s->efuse_wr.blk1[idx] & ~(s->efuse_rd_dis.blk1[idx]);
        break;
    case A_EFUSE_BLK2_RDATA0 ... A_EFUSE_BLK3_RDATA0 - 4:
        idx = (addr - A_EFUSE_BLK2_RDATA0) / 4;
        r = s->efuse_wr.blk2[idx] & ~(s->efuse_rd_dis.blk2[idx]);
        break;
    case A_EFUSE_BLK3_RDATA0 ... A_EFUSE_CLK - 4:
        idx = (addr - A_EFUSE_BLK3_RDATA0) / 4;
        r = s->efuse_wr.blk3[idx] & ~(s->efuse_rd_dis.blk3[idx]);
        break;
    case A_EFUSE_CLK:
        r = s->clk_reg;
        break;
    case A_EFUSE_CONF:
        r = s->conf_reg;
        break;
    case A_EFUSE_CMD:
        r = s->cmd_reg;
        break;
    case A_EFUSE_STATUS:
        r = 0;
        break;
    case A_EFUSE_DAC_CONF:
        r = s->dac_conf_reg;
        break;
    case A_EFUSE_INT_RAW:
        r = s->int_raw_reg;
        break;
    case A_EFUSE_INT_ST:
        r = s->int_st_reg;
        break;
    case A_EFUSE_INT_ENA:
        r = s->int_ena_reg;
        break;
    case A_EFUSE_DEC_STATUS:
        r = 0;
        break;
    case A_EFUSE_DATE:
        r = 0x16042600;
    }
    return r;
}

static void esp32_efuse_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32EfuseState *s = ESP32_EFUSE(opaque);
    switch (addr) {
    case A_EFUSE_CLK:
        s->clk_reg = value;
        break;
    case A_EFUSE_CONF:
        s->conf_reg = value;
        break;
    case A_EFUSE_CMD:
        if ((value & EFUSE_READ) && s->conf_reg == EFUSE_READ_OP_CODE) {
            esp32_efuse_read_op(s);
        }
        if ((value & EFUSE_PGM) && s->conf_reg == EFUSE_PGM_OP_CODE) {
            esp32_efuse_program_op(s);
        }
        break;
    case A_EFUSE_DAC_CONF:
        s->dac_conf_reg = value;
        break;
    case A_EFUSE_INT_ENA:
        s->int_ena_reg = value;
        esp32_efuse_update_irq(s);
        break;
    case A_EFUSE_INT_CLR:
        s->int_raw_reg &= ~value;
        esp32_efuse_update_irq(s);
        break;
    case A_EFUSE_BLK0_WDATA0 ... A_EFUSE_BLK1_RDATA0 - 4:
        s->efuse_wr.blk0[(addr - A_EFUSE_BLK0_WDATA0) / 4] = value;
        break;
    case A_EFUSE_BLK1_WDATA0 ... A_EFUSE_BLK2_WDATA0 - 4:
        s->efuse_wr.blk1[(addr - A_EFUSE_BLK1_WDATA0) / 4] = value;
        break;
    case A_EFUSE_BLK2_WDATA0 ... A_EFUSE_BLK3_WDATA0 - 4:
        s->efuse_wr.blk1[(addr - A_EFUSE_BLK2_WDATA0) / 4] = value;
        break;
    case A_EFUSE_BLK3_WDATA0 ... A_EFUSE_CLK - 4:
        s->efuse_wr.blk1[(addr - A_EFUSE_BLK3_WDATA0) / 4] = value;
        break;
    }
}

static void esp32_efuse_read_op(Esp32EfuseState *s)
{
    s->cmd_reg = EFUSE_READ;
    /* TODO: read from storage */
    /* TODO: set s->efuse_wr_dis and s->efuse_rd_dis from s->efuse_rd */
    esp32_efuse_op_timer_start(s);
}

static void esp32_efuse_program_op(Esp32EfuseState *s)
{
    s->cmd_reg = EFUSE_PGM;
    /* TODO: apply write protection */
    /* TODO: write to storage */
    esp32_efuse_op_timer_start(s);
}

static void esp32_efuse_update_irq(Esp32EfuseState *s)
{
    s->int_st_reg = s->int_ena_reg & s->int_raw_reg;
    int level = s->int_st_reg != 0;
    qemu_set_irq(s->irq, level);
}

static void esp32_efuse_op_timer_start(Esp32EfuseState *s)
{
    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t interval_ns = 100000000; /* 10 ms, make this depend on EFUSE_CLK register */
    timer_mod_anticipate_ns(&s->op_timer, ns_now + interval_ns);
}

static void esp32_efuse_timer_cb(void *opaque)
{
    Esp32EfuseState *s = ESP32_EFUSE(opaque);
    uint32_t cmd = s->cmd_reg;
    s->cmd_reg = 0;
    s->int_raw_reg |= cmd;
    esp32_efuse_update_irq(s);
}

static const MemoryRegionOps esp32_efuse_ops = {
    .read =  esp32_efuse_read,
    .write = esp32_efuse_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_efuse_reset(DeviceState *dev)
{
    Esp32EfuseState *s = ESP32_EFUSE(dev);
    esp32_efuse_read_op(s);
}

static void esp32_efuse_realize(DeviceState *dev, Error **errp)
{
    Esp32EfuseState *s = ESP32_EFUSE(dev);
}

static void esp32_efuse_init(Object *obj)
{
    Esp32EfuseState *s = ESP32_EFUSE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_efuse_ops, s,
                          TYPE_ESP32_EFUSE, A_EFUSE_DATE + 4);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->op_timer, QEMU_CLOCK_VIRTUAL, esp32_efuse_timer_cb, s);
}

static Property esp32_efuse_properties[] = {
//    DEFINE_PROP_DRIVE("storage", Esp32EfuseState, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_efuse_reset;
    dc->realize = esp32_efuse_realize;
    dc->props = esp32_efuse_properties;
}

static const TypeInfo esp32_efuse_info = {
    .name = TYPE_ESP32_EFUSE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32EfuseState),
    .instance_init = esp32_efuse_init,
    .class_init = esp32_efuse_class_init
};

static void esp32_efuse_register_types(void)
{
    type_register_static(&esp32_efuse_info);
}

type_init(esp32_efuse_register_types)

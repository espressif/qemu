/*
 * ESP32-H2 MODEM_LPCON (Modem Low-Power Controller) emulation
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
#include "hw/misc/esp32h2_modem.h"

/*
 * ESP32-H2 MODEM_LPCON register offsets (from modem_lpcon_struct.h):
 *   test_conf         0x00
 *   coex_lp_clk_conf  0x04
 *   clk_conf          0x08
 *   clk_conf_force_on 0x0C
 *   tick_conf         0x10
 *   rst_conf          0x14
 *   mem_conf          0x18
 *   date              0x1C
 */
#define CLK_CONF_OFF            0x08
#define CLK_CONF_FORCE_ON_OFF   0x0C
#define RST_CONF_OFF            0x14

/*
 * Clock enable bits that must always read as set.
 * QEMU does not gate peripheral clocks, so firmware clock assertions
 * (e.g. regi2c_ctrl_ll_master_is_clock_enabled) must always pass.
 *   bit 1 – clk_coex_en
 *   bit 2 – clk_i2c_mst_en
 *   bit 5 – clk_fe_mem_en
 */
#define CLK_CONF_ALWAYS_ON_MASK 0x00000026

static uint64_t esp32h2_modem_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H2ModemState *s = ESP32H2_MODEM(opaque);

    switch (addr) {
    case CLK_CONF_OFF:
        return s->clk_conf | CLK_CONF_ALWAYS_ON_MASK;
    case CLK_CONF_FORCE_ON_OFF:
        return s->clk_conf_force_on;
    case RST_CONF_OFF:
        return s->rst_conf;
    default:
        return 0;
    }
}

static void esp32h2_modem_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32H2ModemState *s = ESP32H2_MODEM(opaque);

    switch (addr) {
    case CLK_CONF_OFF:
        s->clk_conf = (uint32_t)value;
        break;
    case CLK_CONF_FORCE_ON_OFF:
        s->clk_conf_force_on = (uint32_t)value;
        break;
    case RST_CONF_OFF:
        s->rst_conf = (uint32_t)value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32h2_modem_ops = {
    .read  = esp32h2_modem_read,
    .write = esp32h2_modem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32h2_modem_realize(DeviceState *dev, Error **errp)
{
    ESP32H2ModemState *s = ESP32H2_MODEM(dev);
    s->clk_conf = 0xFFFFFFFF;
}

static void esp32h2_modem_init(Object *obj)
{
    ESP32H2ModemState *s = ESP32H2_MODEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32h2_modem_ops, s,
                          TYPE_ESP32H2_MODEM, ESP32H2_MODEM_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32h2_modem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = esp32h2_modem_realize;
}

static const TypeInfo esp32h2_modem_info = {
    .name = TYPE_ESP32H2_MODEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H2ModemState),
    .instance_init = esp32h2_modem_init,
    .class_init = esp32h2_modem_class_init,
};

static void esp32h2_modem_register_types(void)
{
    type_register_static(&esp32h2_modem_info);
}

type_init(esp32h2_modem_register_types)

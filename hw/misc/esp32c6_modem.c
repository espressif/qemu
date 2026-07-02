/*
 * ESP32-C6 MODEM_LPCON (Modem Low-Power Controller) emulation
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
#include "hw/misc/esp32c6_modem.h"

#define CLK_CONF_OFF            0x18
#define CLK_CONF_FORCE_ON_OFF   0x1c
#define RST_CONF_OFF            0x24
#define MEM_CONF_OFF            0x28
#define DATE_OFF                0x2c

/*
 * Clock enable bits that must always read as set.
 * QEMU does not gate peripheral clocks, so firmware clock assertions
 * (e.g. regi2c_ctrl_ll_master_is_clock_enabled) must always pass.
 *   bit 0 – clk_wifipwr_en
 *   bit 1 – clk_coex_en
 *   bit 2 – clk_i2c_mst_en
 *   bit 3 – clk_lp_timer_en
 */
#define CLK_CONF_ALWAYS_ON_MASK 0x0000000F

static uint64_t esp32c6_modem_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C6ModemState *s = ESP32C6_MODEM(opaque);

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

static void esp32c6_modem_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32C6ModemState *s = ESP32C6_MODEM(opaque);

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

static const MemoryRegionOps esp32c6_modem_ops = {
    .read  = esp32c6_modem_read,
    .write = esp32c6_modem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_modem_realize(DeviceState *dev, Error **errp)
{
    ESP32C6ModemState *s = ESP32C6_MODEM(dev);
    /* All modem clocks enabled by default so IDF clock assertions pass */
    s->clk_conf = 0xFFFFFFFF;
}

static void esp32c6_modem_init(Object *obj)
{
    ESP32C6ModemState *s = ESP32C6_MODEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_modem_ops, s,
                          TYPE_ESP32C6_MODEM, ESP32C6_MODEM_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c6_modem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = esp32c6_modem_realize;
}

static const TypeInfo esp32c6_modem_info = {
    .name = TYPE_ESP32C6_MODEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6ModemState),
    .instance_init = esp32c6_modem_init,
    .class_init = esp32c6_modem_class_init,
};

static void esp32c6_modem_register_types(void)
{
    type_register_static(&esp32c6_modem_info);
}

type_init(esp32c6_modem_register_types)

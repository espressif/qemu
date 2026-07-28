/*
 * ESP32-P4 HP_SYS_CLKRST
 *
 * Standalone HP system clock/reset controller (not derived from PCR).
 * Handles ROOT_CLK_CTRL0 SOC_CLK_DIV_UPDATE auto-clear and peripheral
 * ready-bit helpers used by the bootloader.
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
#include "hw/misc/esp32p4_hp_sys_clkrst.h"

#define P4_ROOT_CLK_CTRL0_REG   0x004
#define P4_SOC_CLK_DIV_UPDATE   (1U << 4)
#define P4_ORIGIN_REG           0x3F8

static uint64_t esp32p4_hp_sys_clkrst_read(void *opaque, hwaddr addr,
                                           unsigned int size)
{
    ESP32P4HpSysClkrstState *s = ESP32P4_HP_SYS_CLKRST(opaque);

    if (addr == P4_ORIGIN_REG) {
        return 0x51454d55; /* "QEMU" */
    }

    if (addr < ESP32P4_HP_SYS_CLKRST_IO_SIZE) {
        uint32_t val = s->reg[addr / 4];
        /* When reset-style bit1 is cleared, report ready bits 2|3 */
        if (!(val & (1 << 1))) {
            val |= (1 << 2) | (1 << 3);
        }
        return val;
    }

    return 0;
}

static void esp32p4_hp_sys_clkrst_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned int size)
{
    ESP32P4HpSysClkrstState *s = ESP32P4_HP_SYS_CLKRST(opaque);

    if (addr < ESP32P4_HP_SYS_CLKRST_IO_SIZE) {
        s->reg[addr / 4] = (uint32_t)value;

        if (addr == P4_ROOT_CLK_CTRL0_REG) {
            /* SOC_CLK_DIV_UPDATE auto-clears after the update is applied */
            s->reg[addr / 4] &= ~P4_SOC_CLK_DIV_UPDATE;
        }
    }
}

static const MemoryRegionOps esp32p4_hp_sys_clkrst_ops = {
    .read = esp32p4_hp_sys_clkrst_read,
    .write = esp32p4_hp_sys_clkrst_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32p4_hp_sys_clkrst_init(Object *obj)
{
    ESP32P4HpSysClkrstState *s = ESP32P4_HP_SYS_CLKRST(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32p4_hp_sys_clkrst_ops, s,
                          TYPE_ESP32P4_HP_SYS_CLKRST,
                          ESP32P4_HP_SYS_CLKRST_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const TypeInfo esp32p4_hp_sys_clkrst_info = {
    .name = TYPE_ESP32P4_HP_SYS_CLKRST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32P4HpSysClkrstState),
    .instance_init = esp32p4_hp_sys_clkrst_init,
};

static void esp32p4_hp_sys_clkrst_register_types(void)
{
    type_register_static(&esp32p4_hp_sys_clkrst_info);
}

type_init(esp32p4_hp_sys_clkrst_register_types)

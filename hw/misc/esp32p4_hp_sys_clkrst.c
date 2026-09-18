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
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32p4_hp_sys_clkrst.h"

#define P4_ROOT_CLK_CTRL0_REG   0x004
#define P4_SOC_CLK_DIV_UPDATE   (1U << 4)
#define P4_ORIGIN_REG           0x3F8

/*
 * Registers that must read back their true silicon value because the guest
 * tests individual bits in them. They are excluded from the generic
 * "reset deasserted implies ready" fudge applied to every other offset.
 */
#define P4_SOC_CLK_CTRL0_REG    0x014
#define P4_CORE1_CPU_CLK_EN     (1U << 4)
#define P4_ANA_PLL_CTRL0_REG    0x0BC
/* PLLA, CPU, SDIO, SYS and MSPI PLL calibration: finishes at once in the model */
#define P4_ANA_PLL_CAL_END_ALL  0x155
#define P4_HP_RST_EN0_REG       0x0C0
#define P4_RST_EN_CORE1_GLOBAL  (1U << 8)

/* Reset values, from IDF register/hw_ver3/soc/hp_sys_clkrst_reg.h */
static const struct {
    hwaddr offset;
    uint32_t value;
} hp_sys_clkrst_reset[] = {
    /* CORE0/CORE1 CLIC, MISC, CORE0 CPU, TCM and L1CACHE clocks come up
     * enabled; CORE1's CPU clock (bit 4) does not. */
    { P4_SOC_CLK_CTRL0_REG, 0x000000AF },
    /* Only RST_EN_CORE1_GLOBAL is asserted out of reset: core1 is held off
     * until core0 releases it. */
    { P4_HP_RST_EN0_REG,    P4_RST_EN_CORE1_GLOBAL },
};

static bool esp32p4_core1_released(ESP32P4HpSysClkrstState *s)
{
    return (s->reg[P4_SOC_CLK_CTRL0_REG / 4] & P4_CORE1_CPU_CLK_EN) &&
           !(s->reg[P4_HP_RST_EN0_REG / 4] & P4_RST_EN_CORE1_GLOBAL);
}

static uint64_t esp32p4_hp_sys_clkrst_read(void *opaque, hwaddr addr,
                                           unsigned int size)
{
    ESP32P4HpSysClkrstState *s = ESP32P4_HP_SYS_CLKRST(opaque);

    if (addr == P4_ORIGIN_REG) {
        return 0x51454d55; /* "QEMU" */
    }

    if (addr < ESP32P4_HP_SYS_CLKRST_IO_SIZE) {
        uint32_t val = s->reg[addr / 4];
        if (addr == P4_ANA_PLL_CTRL0_REG) {
            return val | P4_ANA_PLL_CAL_END_ALL;
        }
        if (addr == P4_SOC_CLK_CTRL0_REG || addr == P4_HP_RST_EN0_REG) {
            return val;
        }
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

        if (addr == P4_SOC_CLK_CTRL0_REG || addr == P4_HP_RST_EN0_REG) {
            qemu_set_irq(s->core1_run, esp32p4_core1_released(s) ? 1 : 0);
        }
    }
}

static void esp32p4_hp_sys_clkrst_reset_hold(Object *obj, ResetType type)
{
    ESP32P4HpSysClkrstState *s = ESP32P4_HP_SYS_CLKRST(obj);

    memset(s->reg, 0, sizeof(s->reg));
    for (size_t i = 0; i < ARRAY_SIZE(hp_sys_clkrst_reset); i++) {
        s->reg[hp_sys_clkrst_reset[i].offset / 4] =
            hp_sys_clkrst_reset[i].value;
    }

    qemu_set_irq(s->core1_run, esp32p4_core1_released(s) ? 1 : 0);
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

    qdev_init_gpio_out_named(DEVICE(s), &s->core1_run,
                             ESP32P4_HP_SYS_CLKRST_CORE1_RUN_GPIO, 1);
}

static void esp32p4_hp_sys_clkrst_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32p4_hp_sys_clkrst_reset_hold;
}

static const TypeInfo esp32p4_hp_sys_clkrst_info = {
    .name = TYPE_ESP32P4_HP_SYS_CLKRST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32P4HpSysClkrstState),
    .instance_init = esp32p4_hp_sys_clkrst_init,
    .class_init = esp32p4_hp_sys_clkrst_class_init,
};

static void esp32p4_hp_sys_clkrst_register_types(void)
{
    type_register_static(&esp32p4_hp_sys_clkrst_info);
}

type_init(esp32p4_hp_sys_clkrst_register_types)

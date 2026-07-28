/*
 * ESP32-P4 LP (Low Power) domain controller emulation
 *
 * Absolute MMIO is mapped at ESP32P4_LP_BASE by the machine; this device
 * uses relative offsets from that base.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/guest-random.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/misc/esp32p4_lp.h"

/* LP_CLKRST is at +0x1000 from LP_BASE; RESET_CAUSE at +0x10 within it */
#define LP_CLKRST_RESET_CAUSE_REG  0x1010

/* LP_TRNG at DR_REG_LP_TRNG_BASE (0x50126000) → relative 0x16000 */
#define LP_TRNG_DATA_REG           0x16000
#define LP_TRNG_DATA_REG_HI        0x16004

/*
 * LPPERI registers whose reset value is non-zero. LPPERI_CLK_EN_REG in
 * particular comes out of reset with every LP peripheral clock enabled;
 * regi2c asserts on CK_EN_LP_I2CMST (bit 27) before touching I2C_ANA_MST.
 */
static const struct {
    hwaddr offset;
    uint32_t value;
} lpperi_reset[] = {
    { 0x000, 0x7FFF0000 },  /* CLK_EN */
    { 0x00C, 0x80000000 },  /* CPU */
    { 0x028, 0x80000000 },  /* MEM_CTRL */
    { 0x02C, 0x04040400 },  /* ADC_CTRL */
    { 0x030, 0x02000000 },  /* LP_I2S_RXCLK_DIV_NUM */
    { 0x034, 0x00004000 },  /* LP_I2S_RXCLK_DIV_XYZ */
    { 0x038, 0x02000000 },  /* LP_I2S_TXCLK_DIV_NUM */
    { 0x03C, 0x00004000 },  /* LP_I2S_TXCLK_DIV_XYZ */
};

static void esp32p4_lp_reset_request(void *opaque, int n, int level)
{
    ESP32P4LpState *s = ESP32P4_LP(opaque);
    assert(n < ESP32P4_RESET_REASON_MAX);
    if (level) {
        s->reset_reason = n;
        qemu_irq_raise(s->cpu_reset);
    }
}

static uint64_t esp32p4_lp_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32P4LpState *s = ESP32P4_LP(opaque);
    uint64_t r = 0;

    if (addr == LP_CLKRST_RESET_CAUSE_REG) {
        uint32_t reason = s->reset_reason & 0x3f;
        /* HPCORE0_RESET_CAUSE in bits[12:7]; LPCORE in bits[5:0] */
        r = (reason << 7) | reason;
        return r;
    }

    if (addr == LP_TRNG_DATA_REG || addr == LP_TRNG_DATA_REG_HI) {
        uint32_t rng;
        qemu_guest_getrandom_nofail(&rng, sizeof(rng));
        return rng;
    }

    if (addr >= ESP32P4_LPPERI_OFFSET &&
        addr < ESP32P4_LPPERI_OFFSET + ESP32P4_LPPERI_SIZE) {
        return s->lpperi[(addr - ESP32P4_LPPERI_OFFSET) / 4];
    }

    return 0;
}

static void esp32p4_lp_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    ESP32P4LpState *s = ESP32P4_LP(opaque);

    if (addr >= ESP32P4_LPPERI_OFFSET &&
        addr < ESP32P4_LPPERI_OFFSET + ESP32P4_LPPERI_SIZE) {
        s->lpperi[(addr - ESP32P4_LPPERI_OFFSET) / 4] = (uint32_t)value;
    }
}

static const MemoryRegionOps esp32p4_lp_ops = {
    .read  = esp32p4_lp_read,
    .write = esp32p4_lp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32p4_lp_reset_hold(Object *obj, ResetType type)
{
    static bool first_boot = true;
    ESP32P4LpState *s = ESP32P4_LP(obj);

    if (first_boot) {
        s->reset_reason = ESP32P4_CHIP_POWER_ON;
        first_boot = false;
    }

    memset(s->lpperi, 0, sizeof(s->lpperi));
    for (size_t i = 0; i < ARRAY_SIZE(lpperi_reset); i++) {
        s->lpperi[lpperi_reset[i].offset / 4] = lpperi_reset[i].value;
    }

    qemu_irq_lower(s->cpu_reset);
}

static void esp32p4_lp_init(Object *obj)
{
    ESP32P4LpState *s = ESP32P4_LP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32p4_lp_ops, s,
                          TYPE_ESP32P4_LP, ESP32P4_LP_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(DEVICE(s), esp32p4_lp_reset_request, ESP32P4_RESET_REASON_MAX);
    qdev_init_gpio_out_named(DEVICE(s), &s->cpu_reset, ESP32P4_LP_RESET_GPIO, 1);

    s->reset_reason = ESP32P4_CHIP_POWER_ON;
}

static void esp32p4_lp_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32p4_lp_reset_hold;
}

static const TypeInfo esp32p4_lp_info = {
    .name = TYPE_ESP32P4_LP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32P4LpState),
    .instance_init = esp32p4_lp_init,
    .class_init = esp32p4_lp_class_init,
};

static void esp32p4_lp_register_types(void)
{
    type_register_static(&esp32p4_lp_info);
}

type_init(esp32p4_lp_register_types)

/*
 * ESP32-C61 LP (Low Power) domain controller emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/guest-random.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/misc/esp32c61_lp.h"

#define LP_CLKRST_BASE    0x0400
#define LP_CLKRST_END     0x0500
#define LP_CLKRST_RESET_CAUSE_REG  0x0410

#define LP_TIMER_BASE     0x0C00
#define LP_TIMER_END      0x0D00
#define LP_TIMER_UPDATE_REG    0x0C10
#define LP_TIMER_COUNTER_LO    0x0C14
#define LP_TIMER_COUNTER_HI    0x0C18

#define LP_AON_BASE       0x1000
#define LP_AON_END        0x1100
#define LP_AON_STORE0_REG 0x1000
#define LP_AON_STORE4_REG 0x1010
#define LP_AON_STORE4_RESET 0x00280028
#define LP_AON_SYS_CFG_REG     0x1034
#define LP_AON_HPSYS_SW_RESET_BIT (1U << 31)
#define LP_AON_CPUCORE0_CFG_REG 0x1038
#define LP_AON_CPU_CORE0_SW_RESET_BIT  (1 << 28)

#define RTC_I2C_BASE      0x1800
#define RTC_I2C_END       0x1900

#define LPPERI_BASE            0x2800
#define LPPERI_RNG_DATA_REG    0x2808
#define LPPERI_RNG_DATA_SYNC   0x2828

static void esp32c61_lp_reset_request(void *opaque, int n, int level)
{
    ESP32C61LpState *s = ESP32C61_LP(opaque);
    assert(n < ESP32C61_RESET_REASON_MAX);
    if (level) {
        s->reset_reason = n;
        qemu_irq_raise(s->cpu_reset);
    }
}

static uint64_t esp32c61_lp_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C61LpState *s = ESP32C61_LP(opaque);
    uint64_t r = 0;

    if (addr >= LP_CLKRST_BASE && addr < LP_CLKRST_END) {
        if (addr == LP_CLKRST_RESET_CAUSE_REG) {
            r = (s->reset_reason & 0x1F) | (1 << 5);
        }
        return r;
    }

    if (addr >= LP_AON_BASE && addr < LP_AON_END) {
        if (addr >= LP_AON_STORE0_REG &&
            addr < LP_AON_STORE0_REG + sizeof(s->aon_store)) {
            r = s->aon_store[(addr - LP_AON_STORE0_REG) / 4];
        }
        return r;
    }

    if (addr >= LP_TIMER_BASE && addr < LP_TIMER_END) {
        if (addr == LP_TIMER_COUNTER_LO) {
            r = (uint32_t)(s->lp_timer_counter & 0xFFFFFFFF);
        } else if (addr == LP_TIMER_COUNTER_HI) {
            r = (uint32_t)((s->lp_timer_counter >> 32) & 0xFFFF);
        }
        return r;
    }

    if (addr >= RTC_I2C_BASE && addr < RTC_I2C_END) {
        return 0x00FFFFFF;
    }

    if (addr == LPPERI_RNG_DATA_REG || addr == LPPERI_RNG_DATA_SYNC) {
        uint32_t rng;
        qemu_guest_getrandom_nofail(&rng, sizeof(rng));
        return rng;
    }

    return 0;
}

static void esp32c61_lp_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32C61LpState *s = ESP32C61_LP(opaque);

    if (addr >= LP_AON_STORE0_REG &&
        addr < LP_AON_STORE0_REG + sizeof(s->aon_store)) {
        s->aon_store[(addr - LP_AON_STORE0_REG) / 4] = value;
    } else if (addr == LP_TIMER_UPDATE_REG) {
        if (value & (1 << 27)) {
            s->lp_timer_counter = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 6667;
        }
    } else if (addr == LP_AON_SYS_CFG_REG) {
        if (value & LP_AON_HPSYS_SW_RESET_BIT) {
            esp32c61_lp_reset_request(opaque, ESP32C61_CORE_SW, 1);
        }
    } else if (addr == LP_AON_CPUCORE0_CFG_REG) {
        if (value & LP_AON_CPU_CORE0_SW_RESET_BIT) {
            esp32c61_lp_reset_request(opaque, ESP32C61_CORE_SW, 1);
        }
    }
}

static const MemoryRegionOps esp32c61_lp_ops = {
    .read  = esp32c61_lp_read,
    .write = esp32c61_lp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c61_lp_reset_hold(Object *obj, ResetType type)
{
    static bool first_boot = true;
    ESP32C61LpState *s = ESP32C61_LP(obj);

    if (first_boot) {
        s->reset_reason = ESP32C61_CHIP_POWER_ON;
        first_boot = false;
    }

    qemu_irq_lower(s->cpu_reset);
}

static void esp32c61_lp_init(Object *obj)
{
    ESP32C61LpState *s = ESP32C61_LP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c61_lp_ops, s,
                          TYPE_ESP32C61_LP, ESP32C61_LP_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(DEVICE(s), esp32c61_lp_reset_request, ESP32C61_RESET_REASON_MAX);
    qdev_init_gpio_out_named(DEVICE(s), &s->cpu_reset, ESP32C61_LP_RESET_GPIO, 1);

    s->reset_reason = ESP32C61_CHIP_POWER_ON;
    s->aon_store[(LP_AON_STORE4_REG - LP_AON_STORE0_REG) / 4] = LP_AON_STORE4_RESET;
}

static void esp32c61_lp_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32c61_lp_reset_hold;
}

static const TypeInfo esp32c61_lp_info = {
    .name = TYPE_ESP32C61_LP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C61LpState),
    .instance_init = esp32c61_lp_init,
    .class_init = esp32c61_lp_class_init,
};

static void esp32c61_lp_register_types(void)
{
    type_register_static(&esp32c61_lp_info);
}

type_init(esp32c61_lp_register_types)

/*
 * ESP32-H4 LP (Low Power) domain controller emulation
 *
 * Window relative to 0x600B0000. RNG at TRNG (0x2000), reset cause at
 * LP_CLKRST (0x3010), LP_AON controls at 0x2800+.
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
#include "hw/misc/esp32h4_lp.h"

#define TRNG_BASE              0x2000
#define TRNG_DATA_REG          0x2000
#define TRNG_DATA_SYNC_REG     0x2008

#define LP_AON_BASE            0x2800
#define LP_AON_END             0x2900
#define LP_AON_STORE4_REG      0x2810
#define LP_AON_SYS_CFG_REG     0x2834
#define LP_AON_HPSYS_SW_RESET_BIT (1U << 31)
#define LP_AON_CPUCORE0_CFG_REG 0x2838
#define LP_AON_CPU_CORE0_SW_RESET_BIT  (1U << 8)

#define LP_CLKRST_BASE         0x3000
#define LP_CLKRST_END          0x3100
#define LP_CLKRST_RESET_CAUSE_REG  0x3010

#define LP_TIMER_BASE          0x5000
#define LP_TIMER_END           0x5100
#define LP_TIMER_UPDATE_REG     0x5010
#define LP_TIMER_COUNTER_LO    0x5014
#define LP_TIMER_COUNTER_HI    0x5018

static void esp32h4_lp_reset_request(void *opaque, int n, int level)
{
    ESP32H4LpState *s = ESP32H4_LP(opaque);
    assert(n < ESP32H4_RESET_REASON_MAX);
    if (level) {
        s->reset_reason = n;
        qemu_irq_raise(s->cpu_reset);
    }
}

static uint64_t esp32h4_lp_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H4LpState *s = ESP32H4_LP(opaque);
    uint64_t r = 0;

    if (addr == TRNG_DATA_REG || addr == TRNG_DATA_SYNC_REG) {
        uint32_t rng;
        qemu_guest_getrandom_nofail(&rng, sizeof(rng));
        return rng;
    }

    if (addr >= LP_CLKRST_BASE && addr < LP_CLKRST_END) {
        if (addr == LP_CLKRST_RESET_CAUSE_REG) {
            r = (s->reset_reason & 0x1F) | (1 << 5);
        }
        return r;
    }

    if (addr >= LP_AON_BASE && addr < LP_AON_END) {
        if (addr == LP_AON_STORE4_REG) {
            r = 0x00200020; /* 32 MHz XTAL hint */
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

    return 0;
}

static void esp32h4_lp_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    ESP32H4LpState *s = ESP32H4_LP(opaque);

    if (addr == LP_TIMER_UPDATE_REG) {
        if (value & (1 << 28)) {
            s->lp_timer_counter = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 6667;
        }
    } else if (addr == LP_AON_SYS_CFG_REG) {
        if (value & LP_AON_HPSYS_SW_RESET_BIT) {
            esp32h4_lp_reset_request(opaque, ESP32H4_CORE_SW, 1);
        }
    } else if (addr == LP_AON_CPUCORE0_CFG_REG) {
        if (value & LP_AON_CPU_CORE0_SW_RESET_BIT) {
            esp32h4_lp_reset_request(opaque, ESP32H4_CORE_SW, 1);
        }
    }
}

static const MemoryRegionOps esp32h4_lp_ops = {
    .read  = esp32h4_lp_read,
    .write = esp32h4_lp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32h4_lp_reset_hold(Object *obj, ResetType type)
{
    static bool first_boot = true;
    ESP32H4LpState *s = ESP32H4_LP(obj);

    if (first_boot) {
        s->reset_reason = ESP32H4_CHIP_POWER_ON;
        first_boot = false;
    }

    qemu_irq_lower(s->cpu_reset);
}

static void esp32h4_lp_init(Object *obj)
{
    ESP32H4LpState *s = ESP32H4_LP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32h4_lp_ops, s,
                          TYPE_ESP32H4_LP, ESP32H4_LP_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(DEVICE(s), esp32h4_lp_reset_request, ESP32H4_RESET_REASON_MAX);
    qdev_init_gpio_out_named(DEVICE(s), &s->cpu_reset, ESP32H4_LP_RESET_GPIO, 1);

    s->reset_reason = ESP32H4_CHIP_POWER_ON;
}

static void esp32h4_lp_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32h4_lp_reset_hold;
}

static const TypeInfo esp32h4_lp_info = {
    .name = TYPE_ESP32H4_LP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H4LpState),
    .instance_init = esp32h4_lp_init,
    .class_init = esp32h4_lp_class_init,
};

static void esp32h4_lp_register_types(void)
{
    type_register_static(&esp32h4_lp_info);
}

type_init(esp32h4_lp_register_types)

/*
 * ESP32-H2 LP (Low Power) domain controller emulation
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
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/misc/esp32h2_lp.h"

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
#define LP_AON_STORE4_REG 0x1010
#define LP_AON_CPUCORE0_CFG_REG 0x1038
#define LP_AON_CPU_CORE0_SW_RESET_BIT  (1 << 28)

#define LP_ANALOG_PERI_BASE  0x2C00
#define LP_ANALOG_PERI_END   0x2C20
#define LP_ANA_FIB_ENABLE_REG  0x2C1C

static void esp32h2_lp_reset_request(void *opaque, int n, int level)
{
    ESP32H2LpState *s = ESP32H2_LP(opaque);
    assert(n < ESP32H2_RESET_REASON_MAX);
    if (level) {
        s->reset_reason = n;
        qemu_irq_raise(s->cpu_reset);
    }
}

static uint64_t esp32h2_lp_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H2LpState *s = ESP32H2_LP(opaque);
    uint64_t r = 0;

    if (addr >= LP_CLKRST_BASE && addr < LP_CLKRST_END) {
        if (addr == LP_CLKRST_RESET_CAUSE_REG) {
            r = s->reset_reason;
        }
        return r;
    }

    if (addr >= LP_AON_BASE && addr < LP_AON_END) {
        if (addr == LP_AON_STORE4_REG) {
            r = 0x00200020;
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

    if (addr >= LP_ANALOG_PERI_BASE && addr < LP_ANALOG_PERI_END) {
        if (addr == LP_ANA_FIB_ENABLE_REG) {
            return 0xFFFFFFFF;
        }
        return 0;
    }

    return 0;
}

static void esp32h2_lp_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    ESP32H2LpState *s = ESP32H2_LP(opaque);

    if (addr == LP_TIMER_UPDATE_REG) {
        if (value & (1 << 28)) {
            s->lp_timer_counter = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 6667;
        }
    } else if (addr == LP_AON_CPUCORE0_CFG_REG) {
        if (value & LP_AON_CPU_CORE0_SW_RESET_BIT) {
            esp32h2_lp_reset_request(opaque, ESP32H2_CORE_SW, 1);
        }
    }
}

static const MemoryRegionOps esp32h2_lp_ops = {
    .read  = esp32h2_lp_read,
    .write = esp32h2_lp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32h2_lp_reset_hold(Object *obj, ResetType type)
{
    static bool first_boot = true;
    ESP32H2LpState *s = ESP32H2_LP(obj);

    if (first_boot) {
        s->reset_reason = ESP32H2_CHIP_POWER_ON;
        first_boot = false;
    }

    qemu_irq_lower(s->cpu_reset);
}

static void esp32h2_lp_init(Object *obj)
{
    ESP32H2LpState *s = ESP32H2_LP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32h2_lp_ops, s,
                          TYPE_ESP32H2_LP, ESP32H2_LP_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(DEVICE(s), esp32h2_lp_reset_request, ESP32H2_RESET_REASON_MAX);
    qdev_init_gpio_out_named(DEVICE(s), &s->cpu_reset, ESP32H2_LP_RESET_GPIO, 1);

    s->reset_reason = ESP32H2_CHIP_POWER_ON;
}

static void esp32h2_lp_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32h2_lp_reset_hold;
}

static const TypeInfo esp32h2_lp_info = {
    .name = TYPE_ESP32H2_LP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H2LpState),
    .instance_init = esp32h2_lp_init,
    .class_init = esp32h2_lp_class_init,
};

static void esp32h2_lp_register_types(void)
{
    type_register_static(&esp32h2_lp_info);
}

type_init(esp32h2_lp_register_types)

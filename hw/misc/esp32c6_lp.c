/*
 * ESP32-C6 LP (Low Power) domain controller emulation
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
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/misc/esp32c6_lp.h"

/* ======================================================================
 * ESP32-C6 LP register layout
 * From ESP-IDF: components/soc/esp32c6/register/soc/lp_*_reg.h
 * ====================================================================== */

/* LP CLKRST sub-block: 0x0400 - 0x04FF */
REG32(LP_CLKRST_RESET_CAUSE, 0x0410)

/* LP TIMER sub-block: 0x0C00 - 0x0CFF */
REG32(LP_TIMER_UPDATE,     0x0C10)
    FIELD(LP_TIMER_UPDATE, MAIN_TIMER_UPDATE, 28, 1)
REG32(LP_TIMER_COUNTER_LO, 0x0C14)
REG32(LP_TIMER_COUNTER_HI, 0x0C18)

/* LP AON sub-block: 0x1000 - 0x10FF */
REG32(LP_AON_STORE4,         0x1010)
REG32(LP_AON_CPUCORE0_CFG,   0x1038)
    FIELD(LP_AON_CPUCORE0_CFG, CPU_CORE0_SW_RESET, 28, 1)

/* RTC I2C sub-block: 0x1800 - 0x18FF */
#define LP_RTC_I2C_BASE  0x1800
#define LP_RTC_I2C_END   0x1900

/*
 * The LP timer in the ESP32-C6 is sourced from a slow clock that runs at
 * roughly 150 kHz (RTC_SLOW_CLK).  One tick therefore takes ~6667 ns
 * (1 / 150 kHz). We approximate the counter from the QEMU virtual clock
 * by dividing the elapsed time in ns by this period.
 */
#define ESP32C6_LP_TIMER_TICK_NS 6667

static void esp32c6_lp_reset_request(void *opaque, int n, int level)
{
    ESP32C6LpState *s = ESP32C6_LP(opaque);
    assert(n < ESP32C6_RESET_REASON_MAX);
    if (level) {
        s->reset_reason = n;
        qemu_irq_raise(s->cpu_reset);
    }
}

static uint64_t esp32c6_lp_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C6LpState *s = ESP32C6_LP(opaque);

    switch (addr) {
    case A_LP_CLKRST_RESET_CAUSE:
        return s->reset_reason;

    case A_LP_AON_STORE4:
        /* Magic value expected by IDF early-boot code */
        return 0x00280028;

    case A_LP_TIMER_COUNTER_LO:
        return (uint32_t)(s->lp_timer_counter & 0xFFFFFFFF);

    case A_LP_TIMER_COUNTER_HI:
        return (uint32_t)((s->lp_timer_counter >> 32) & 0xFFFF);

    default:
        /* The RTC I2C block is accessed during early-boot; return all-ones
         * so that polling loops checking for "ready" exit promptly. */
        if (addr >= LP_RTC_I2C_BASE && addr < LP_RTC_I2C_END) {
            return 0x00FFFFFF;
        }
        return 0;
    }
}

static void esp32c6_lp_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    ESP32C6LpState *s = ESP32C6_LP(opaque);

    switch (addr) {
    case A_LP_TIMER_UPDATE:
        if (FIELD_EX32(value, LP_TIMER_UPDATE, MAIN_TIMER_UPDATE)) {
            s->lp_timer_counter =
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / ESP32C6_LP_TIMER_TICK_NS;
        }
        break;

    case A_LP_AON_CPUCORE0_CFG:
        if (FIELD_EX32(value, LP_AON_CPUCORE0_CFG, CPU_CORE0_SW_RESET)) {
            esp32c6_lp_reset_request(opaque, ESP32C6_CORE_SW, 1);
        }
        break;

    default:
        break;
    }
}

static const MemoryRegionOps esp32c6_lp_ops = {
    .read  = esp32c6_lp_read,
    .write = esp32c6_lp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_lp_reset_hold(Object *obj, ResetType type)
{
    static bool first_boot = true;
    ESP32C6LpState *s = ESP32C6_LP(obj);

    if (first_boot) {
        s->reset_reason = ESP32C6_CHIP_POWER_ON;
        first_boot = false;
    }

    qemu_irq_lower(s->cpu_reset);
}

static void esp32c6_lp_init(Object *obj)
{
    ESP32C6LpState *s = ESP32C6_LP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_lp_ops, s,
                          TYPE_ESP32C6_LP, ESP32C6_LP_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(DEVICE(s), esp32c6_lp_reset_request, ESP32C6_RESET_REASON_MAX);
    qdev_init_gpio_out_named(DEVICE(s), &s->cpu_reset, ESP32C6_LP_RESET_GPIO, 1);

    s->reset_reason = ESP32C6_CHIP_POWER_ON;
}

static void esp32c6_lp_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32c6_lp_reset_hold;
}

static const TypeInfo esp32c6_lp_info = {
    .name = TYPE_ESP32C6_LP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6LpState),
    .instance_init = esp32c6_lp_init,
    .class_init = esp32c6_lp_class_init,
};

static void esp32c6_lp_register_types(void)
{
    type_register_static(&esp32c6_lp_info);
}

type_init(esp32c6_lp_register_types)

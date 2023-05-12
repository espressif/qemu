/*
 * ESP32-C3 CPU Clock and Reset emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/esp32c3_clk.h"

#define CLOCK_DEBUG      0
#define CLOCK_WARNING    0

static uint32_t esp32c3_read_cpu_intr(ESP32C3ClockState *s, uint32_t index)
{
    return (s->levels >> index) & 1;
}


static void esp32c3_write_cpu_intr(ESP32C3ClockState *s, uint32_t index, uint32_t value)
{
    const uint32_t field = FIELD_EX32(value, SYSTEM_CPU_INTR_FROM_CPU_0, CPU_INTR_FROM_CPU_0);
    if (field) {
        s->levels |= BIT(index);
        qemu_set_irq(s->irqs[index], 1);
    } else {
        s->levels &= ~BIT(index);
        qemu_set_irq(s->irqs[index], 0);
    }
}


static uint64_t esp32c3_clock_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3ClockState *s = ESP32C3_CLOCK(opaque);
    uint64_t r = 0;

    switch(addr) {
        case A_SYSTEM_CPU_PER_CONF:
            r = s->cpuperconf;
            break;
        case A_SYSTEM_SYSCLK_CONF:
            r = s->sysclk;
            break;
        case A_SYSTEM_CPU_INTR_FROM_CPU_0:
        case A_SYSTEM_CPU_INTR_FROM_CPU_1:
        case A_SYSTEM_CPU_INTR_FROM_CPU_2:
        case A_SYSTEM_CPU_INTR_FROM_CPU_3:
            r = esp32c3_read_cpu_intr(s, (addr - A_SYSTEM_CPU_INTR_FROM_CPU_0) / sizeof(uint32_t));
            break;
        default:
#if CLOCK_WARNING
            warn_report("[CLOCK] Unsupported read from %08lx\n", addr);
#endif
            break;
    }
    return r;
}

static void esp32c3_clock_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32C3ClockState *s = ESP32C3_CLOCK(opaque);

    switch(addr) {
        case A_SYSTEM_CPU_INTR_FROM_CPU_0:
        case A_SYSTEM_CPU_INTR_FROM_CPU_1:
        case A_SYSTEM_CPU_INTR_FROM_CPU_2:
        case A_SYSTEM_CPU_INTR_FROM_CPU_3:
            esp32c3_write_cpu_intr(s, (addr - A_SYSTEM_CPU_INTR_FROM_CPU_0) / sizeof(uint32_t), value);
            break;
        default:
#if CLOCK_WARNING
            warn_report("[CLOCK] Unsupported write to %08lx (%08lx)\n", addr, value);
#endif
            break;
    }
}

static const MemoryRegionOps esp32c3_clock_ops = {
    .read =  esp32c3_clock_read,
    .write = esp32c3_clock_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_clock_reset(DeviceState *dev)
{
    ESP32C3ClockState *s = ESP32C3_CLOCK(dev);
    /* On board reset, set the proper clocks and dividers */
    s->sysclk = ( 1 << R_SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_SHIFT) |
                (ESP32C3_CLK_SEL_PLL << R_SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_SHIFT) |
                (40 << R_SYSTEM_SYSCLK_CONF_CLK_XTAL_FREQ_SHIFT) |
                ( 1 << R_SYSTEM_SYSCLK_CONF_CLK_DIV_EN_SHIFT);

    /* Divider for PLL clock and APB  frequency */
    s->cpuperconf = (ESP32C3_PERIOD_SEL_80 << R_SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_SHIFT) |
                    (ESP32C3_FREQ_SEL_PLL_480 << R_SYSTEM_CPU_PER_CONF_PLL_FREQ_SEL_SHIFT);

    /* Initialize the IRQs */
    s->levels = 0;
    for (int i = 0 ; i < ESP32C3_SYSTEM_CPU_INTR_COUNT; i++) {
        qemu_irq_lower(s->irqs[i]);
    }
}

static void esp32c3_clock_realize(DeviceState *dev, Error **errp)
{
    /* Initialize the registers */
    esp32c3_clock_reset(dev);
}

static void esp32c3_clock_init(Object *obj)
{
    ESP32C3ClockState *s = ESP32C3_CLOCK(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_clock_ops, s,
                          TYPE_ESP32C3_CLOCK, A_SYSTEM_COMB_PVT_ERR_HVT_SITE3 + sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize the output IRQ lines used to manually trigger interrupts */
    for (uint64_t i = 0; i < ESP32C3_SYSTEM_CPU_INTR_COUNT; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }
}

static void esp32c3_clock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_clock_reset;
    dc->realize = esp32c3_clock_realize;
}

static const TypeInfo esp32c3_cache_info = {
    .name = TYPE_ESP32C3_CLOCK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3ClockState),
    .instance_init = esp32c3_clock_init,
    .class_init = esp32c3_clock_class_init
};

static void esp32c3_cache_register_types(void)
{
    type_register_static(&esp32c3_cache_info);
}

type_init(esp32c3_cache_register_types)

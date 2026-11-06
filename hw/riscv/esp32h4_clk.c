/*
 * ESP32-H4 CPU Clock and Reset
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/riscv/esp32h4_clk.h"

static uint64_t esp32h4_boot_addr_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    ESP32H4ClockState *s = ESP32H4_CLOCK(opaque);

    return s->core1_boot_addr;
}

static void esp32h4_boot_addr_write(void *opaque, hwaddr addr, uint64_t value,
                                    unsigned int size)
{
    ESP32H4ClockState *s = ESP32H4_CLOCK(opaque);

    s->core1_boot_addr = (uint32_t)value;
}

static const MemoryRegionOps esp32h4_boot_addr_ops = {
    .read = esp32h4_boot_addr_read,
    .write = esp32h4_boot_addr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void esp32h4_clock_reset_hold(Object *obj, ResetType type)
{
    ESP32H4ClockState *s = ESP32H4_CLOCK(obj);
    ESP32H4ClockClass *hcc = ESP32H4_CLOCK_GET_CLASS(obj);

    if (hcc->parent_phases.hold) {
        hcc->parent_phases.hold(obj, type);
    }
    s->core1_boot_addr = 0;
}

static void esp32h4_clock_init(Object *obj)
{
    ESP32H4ClockState *s = ESP32H4_CLOCK(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->boot_addr_mr, obj, &esp32h4_boot_addr_ops, s,
                          "esp32h4.hp_system.appcpu_boot_addr", 4);
    sysbus_init_mmio(sbd, &s->boot_addr_mr);
}

static void esp32h4_clock_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ESP32H4ClockClass *hcc = ESP32H4_CLOCK_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, esp32h4_clock_reset_hold,
                                        NULL, &hcc->parent_phases);
}

static const TypeInfo esp32h4_clock_info = {
    .name = TYPE_ESP32H4_CLOCK,
    .parent = TYPE_ESP32C3_CLOCK,
    .instance_size = sizeof(ESP32H4ClockState),
    .instance_init = esp32h4_clock_init,
    .class_init = esp32h4_clock_class_init,
    .class_size = sizeof(ESP32H4ClockClass),
};

static void esp32h4_clock_register_types(void)
{
    type_register_static(&esp32h4_clock_info);
}

type_init(esp32h4_clock_register_types)

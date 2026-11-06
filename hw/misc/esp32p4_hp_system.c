/*
 * ESP32-P4 HP_SYSTEM — software-triggered CPU interrupts
 *
 * Handles CPU_INT_FROM_CPU_x registers used to trigger software IRQs
 * (cross-core / BIST source-map tests). On P4 these sit in HP_SYSTEM at
 * offsets 0x10..0x1c (unlike INTPRI @ 0x90 on C5/H4).
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/misc/esp32p4_hp_system.h"

static uint64_t esp32p4_hp_system_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    ESP32P4HpSystemState *s = ESP32P4_HP_SYSTEM(opaque);

    if (addr >= ESP32P4_HP_SYSTEM_FROM_CPU_OFF &&
        addr < ESP32P4_HP_SYSTEM_FROM_CPU_OFF +
               ESP32P4_HP_SYSTEM_CPU_INTR_COUNT * 4) {
        uint32_t idx = (addr - ESP32P4_HP_SYSTEM_FROM_CPU_OFF) / 4;
        return (s->levels >> idx) & 1;
    }
    return 0;
}

static void esp32p4_hp_system_write(void *opaque, hwaddr addr, uint64_t value,
                                    unsigned int size)
{
    ESP32P4HpSystemState *s = ESP32P4_HP_SYSTEM(opaque);

    if (addr >= ESP32P4_HP_SYSTEM_FROM_CPU_OFF &&
        addr < ESP32P4_HP_SYSTEM_FROM_CPU_OFF +
               ESP32P4_HP_SYSTEM_CPU_INTR_COUNT * 4) {
        uint32_t idx = (addr - ESP32P4_HP_SYSTEM_FROM_CPU_OFF) / 4;
        if (value & 1) {
            s->levels |= BIT(idx);
            qemu_set_irq(s->irqs[idx], 1);
        } else {
            s->levels &= ~BIT(idx);
            qemu_set_irq(s->irqs[idx], 0);
        }
    }
}

static const MemoryRegionOps esp32p4_hp_system_ops = {
    .read = esp32p4_hp_system_read,
    .write = esp32p4_hp_system_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32p4_hp_system_reset_hold(Object *obj, ResetType type)
{
    ESP32P4HpSystemState *s = ESP32P4_HP_SYSTEM(obj);
    s->levels = 0;
    for (int i = 0; i < ESP32P4_HP_SYSTEM_CPU_INTR_COUNT; i++) {
        qemu_irq_lower(s->irqs[i]);
    }
}

static void esp32p4_hp_system_init(Object *obj)
{
    ESP32P4HpSystemState *s = ESP32P4_HP_SYSTEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32p4_hp_system_ops, s,
                          TYPE_ESP32P4_HP_SYSTEM, ESP32P4_HP_SYSTEM_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    for (int i = 0; i < ESP32P4_HP_SYSTEM_CPU_INTR_COUNT; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }
}

static void esp32p4_hp_system_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32p4_hp_system_reset_hold;
}

static const TypeInfo esp32p4_hp_system_info = {
    .name = TYPE_ESP32P4_HP_SYSTEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32P4HpSystemState),
    .instance_init = esp32p4_hp_system_init,
    .class_init = esp32p4_hp_system_class_init,
};

static void esp32p4_hp_system_register_types(void)
{
    type_register_static(&esp32p4_hp_system_info);
}

type_init(esp32p4_hp_system_register_types)

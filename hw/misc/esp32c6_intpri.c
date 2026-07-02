/*
 * ESP32-C6 INTPRI — software-triggered cross-core interrupts
 *
 * Handles the CPU_INTR_FROM_CPU_x registers that FreeRTOS uses to trigger
 * software interrupts for context switching.  These registers sit in the
 * INTPRI block at offsets 0x90..0x9C on the C6.
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
#include "hw/misc/esp32c6_intpri.h"

#define FROM_CPU_OFF  0x90

static uint64_t esp32c6_intpri_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    ESP32C6IntpriState *s = ESP32C6_INTPRI(opaque);

    if (addr >= FROM_CPU_OFF &&
        addr < FROM_CPU_OFF + ESP32C6_INTPRI_CPU_INTR_COUNT * 4) {
        uint32_t idx = (addr - FROM_CPU_OFF) / 4;
        return (s->levels >> idx) & 1;
    }
    return 0;
}

static void esp32c6_intpri_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size)
{
    ESP32C6IntpriState *s = ESP32C6_INTPRI(opaque);

    if (addr >= FROM_CPU_OFF &&
        addr < FROM_CPU_OFF + ESP32C6_INTPRI_CPU_INTR_COUNT * 4) {
        uint32_t idx = (addr - FROM_CPU_OFF) / 4;
        if (value & 1) {
            s->levels |= BIT(idx);
            qemu_set_irq(s->irqs[idx], 1);
        } else {
            s->levels &= ~BIT(idx);
            qemu_set_irq(s->irqs[idx], 0);
        }
    }
}

static const MemoryRegionOps esp32c6_intpri_ops = {
    .read  = esp32c6_intpri_read,
    .write = esp32c6_intpri_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_intpri_reset_hold(Object *obj, ResetType type)
{
    ESP32C6IntpriState *s = ESP32C6_INTPRI(obj);
    s->levels = 0;
    for (int i = 0; i < ESP32C6_INTPRI_CPU_INTR_COUNT; i++) {
        qemu_irq_lower(s->irqs[i]);
    }
}

static void esp32c6_intpri_init(Object *obj)
{
    ESP32C6IntpriState *s = ESP32C6_INTPRI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_intpri_ops, s,
                          TYPE_ESP32C6_INTPRI, ESP32C6_INTPRI_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    for (int i = 0; i < ESP32C6_INTPRI_CPU_INTR_COUNT; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }
}

static void esp32c6_intpri_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32c6_intpri_reset_hold;
}

static const TypeInfo esp32c6_intpri_info = {
    .name          = TYPE_ESP32C6_INTPRI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6IntpriState),
    .instance_init = esp32c6_intpri_init,
    .class_init    = esp32c6_intpri_class_init,
};

static void esp32c6_intpri_register_types(void)
{
    type_register_static(&esp32c6_intpri_info);
}

type_init(esp32c6_intpri_register_types)

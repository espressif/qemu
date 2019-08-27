/*
 * ESP32 Cross-core interrupt
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32_reg.h"
#include "hw/misc/esp32_dport.h"


static uint64_t esp32_crosscore_int_read(void *opaque, hwaddr addr, unsigned int size)
{
    return 0;
}

static void esp32_crosscore_int_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32CrosscoreInt *s = ESP32_CROSSCORE_INT(opaque);
    int index = addr / 4;
    assert(index < ESP32_DPORT_CROSSCORE_INT_COUNT);
    qemu_set_irq(s->irqs[index], value & 0x1);
}

static const MemoryRegionOps esp32_crosscore_int_ops = {
    .read =  esp32_crosscore_int_read,
    .write = esp32_crosscore_int_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_crosscore_int_realize(DeviceState *dev, Error **errp)
{
}

static void esp32_crosscore_int_init(Object *obj)
{
    Esp32CrosscoreInt *s = ESP32_CROSSCORE_INT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_crosscore_int_ops, s,
                          TYPE_ESP32_CROSSCORE_INT,
                          ESP32_DPORT_CROSSCORE_INT_COUNT * sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);

    for (int i = 0; i < ESP32_DPORT_CROSSCORE_INT_COUNT; ++i) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }
}

static void esp32_crosscore_int_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32_crosscore_int_realize;
}

static const TypeInfo esp32_crosscore_int_info = {
    .name = TYPE_ESP32_CROSSCORE_INT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32CrosscoreInt),
    .instance_init = esp32_crosscore_int_init,
    .class_init = esp32_crosscore_int_class_init
};

static void esp32_crosscore_int_register_types(void)
{
    type_register_static(&esp32_crosscore_int_info);
}

type_init(esp32_crosscore_int_register_types)

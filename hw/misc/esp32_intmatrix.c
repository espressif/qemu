/*
 * ESP32 Interrupt Matrix
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
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"
#include "hw/misc/esp32_intmatrix.h"


static void esp32_intmatrix_irq_handler(void *opaque, int n, int level)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    qemu_irq* out = s->outputs[s->irq_map[n]];
    qemu_set_irq(out, level);
}

uint64_t esp32_intmatrix_read(void* opaque, hwaddr addr, unsigned int size)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    int source_index = addr / sizeof(uint32_t);
    if (source_index > ESP32_INT_MATRIX_INPUTS) {
        error_report("%s: source_index %d out of range", __func__, source_index);
        return 0;
    }
    return s->irq_map[source_index];
}

void esp32_intmatrix_write(void* opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    int source_index = addr / sizeof(uint32_t);
    if (source_index > ESP32_INT_MATRIX_INPUTS) {
        error_report("%s: source_index %d out of range", __func__, source_index);
        return;
    }
    s->irq_map[source_index] = (uint8_t) (value & 0x1f);
}

static const MemoryRegionOps esp_intmatrix_ops = {
    .read =  esp32_intmatrix_read,
    .write = esp32_intmatrix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_intmatrix_reset(DeviceState *dev)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(dev);
}

static void esp32_intmatrix_realize(DeviceState *dev, Error **errp)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(dev);

    s->outputs = s->cpu->env.ext_irq_inputs;
    memset(s->irq_map, 0, sizeof(s->irq_map));
}

static void esp32_intmatrix_init(Object *obj)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_intmatrix_ops, s,
                          TYPE_ESP32_INTMATRIX, ESP32_INT_MATRIX_INPUTS * sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(DEVICE(s), esp32_intmatrix_irq_handler, ESP32_INT_MATRIX_INPUTS);
}

static Property esp32_intmatrix_properties[] = {
    DEFINE_PROP_LINK("cpu", Esp32IntMatrixState, cpu, TYPE_XTENSA_CPU, XtensaCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_intmatrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_intmatrix_reset;
    dc->realize = esp32_intmatrix_realize;
    dc->props = esp32_intmatrix_properties;
    dc->user_creatable = false;
}

static const TypeInfo esp32_intmatrix_info = {
    .name = TYPE_ESP32_INTMATRIX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32IntMatrixState),
    .instance_init = esp32_intmatrix_init,
    .class_init = esp32_intmatrix_class_init
};

static void esp32_intmatrix_register_types(void)
{
    type_register_static(&esp32_intmatrix_info);
}

type_init(esp32_intmatrix_register_types)

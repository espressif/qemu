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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/xtensa/esp32_intc.h"

#define INTMATRIX_UNINT_VALUE   6

#define IRQ_MAP(cpu, input) s->irq_map[cpu][input]

static void esp32_intmatrix_irq_handler(void *opaque, int n, int level)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    for (int i = 0; i < ESP32_CPU_COUNT; ++i) {
        if (s->outputs[i] == NULL) {
            continue;
        }
        int out_index = IRQ_MAP(i, n);
        for (int int_index = 0; int_index < s->cpu[i]->env.config->nextint; ++int_index) {
            if (s->cpu[i]->env.config->extint[int_index] == out_index) {
                qemu_set_irq(s->outputs[i][int_index], level);
                break;
            }
        }
    }
}

static inline uint8_t* get_map_entry(Esp32IntMatrixState* s, hwaddr addr)
{
    int source_index = addr / sizeof(uint32_t);
    if (source_index > ESP32_INT_MATRIX_INPUTS * ESP32_CPU_COUNT) {
        error_report("%s: source_index %d out of range", __func__, source_index);
        return NULL;
    }
    int cpu_index = source_index / ESP32_INT_MATRIX_INPUTS;
    source_index = source_index % ESP32_INT_MATRIX_INPUTS;
    return &IRQ_MAP(cpu_index, source_index);
}

static uint64_t esp32_intmatrix_read(void* opaque, hwaddr addr, unsigned int size)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    uint8_t* map_entry = get_map_entry(s, addr);
    return (map_entry != NULL) ? *map_entry : 0;
}

static void esp32_intmatrix_write(void* opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    uint8_t* map_entry = get_map_entry(s, addr);
    if (map_entry != NULL) {
        *map_entry = value & 0x1f;
    }
}

static const MemoryRegionOps esp_intmatrix_ops = {
    .read =  esp32_intmatrix_read,
    .write = esp32_intmatrix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_intmatrix_reset(DeviceState *dev)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(dev);
    memset(s->irq_map, INTMATRIX_UNINT_VALUE, sizeof(s->irq_map));
    for (int i = 0; i < ESP32_CPU_COUNT; ++i) {
        if (s->outputs[i] == NULL) {
            continue;
        }
        for (int int_index = 0; int_index < s->cpu[i]->env.config->nextint; ++int_index) {
            qemu_irq_lower(s->outputs[i][int_index]);
        }
    }

}

static void esp32_intmatrix_realize(DeviceState *dev, Error **errp)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(dev);

    for (int i = 0; i < ESP32_CPU_COUNT; ++i) {
        if (s->cpu[i]) {
            s->outputs[i] = xtensa_get_extints(&s->cpu[i]->env);
        }
    }
    esp32_intmatrix_reset(dev);
}

static void esp32_intmatrix_init(Object *obj)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_intmatrix_ops, s,
                          TYPE_ESP32_INTMATRIX, ESP32_INT_MATRIX_INPUTS * ESP32_CPU_COUNT * sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(DEVICE(s), esp32_intmatrix_irq_handler, ESP32_INT_MATRIX_INPUTS);
}

static Property esp32_intmatrix_properties[] = {
    DEFINE_PROP_LINK("cpu0", Esp32IntMatrixState, cpu[0], TYPE_XTENSA_CPU, XtensaCPU *),
    DEFINE_PROP_LINK("cpu1", Esp32IntMatrixState, cpu[1], TYPE_XTENSA_CPU, XtensaCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_intmatrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_intmatrix_reset;
    dc->realize = esp32_intmatrix_realize;
    device_class_set_props(dc, esp32_intmatrix_properties);
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

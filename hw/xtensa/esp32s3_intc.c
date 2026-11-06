/*
 * ESP32S3 Interrupt Matrix
 *
 * Copyright (c) 2019-2024 Espressif Systems (Shanghai) Co. Ltd.
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
#include "hw/misc/esp32s3_reg.h"
#include "hw/xtensa/esp32s3_intc.h"

#define INTMATRIX_UNINT_VALUE   6

#define INTC_DEBUG      0
#define INTC_WARNING    0

#define IRQ_MAP(cpu, input) s->irq_map[cpu][input]

static void esp32s3_intmatrix_irq_handler(void *opaque, int n, int level)
{
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(opaque);
    if (level) {
        set_bit(n, s->irq_levels);
    } else {
        clear_bit(n, s->irq_levels);
    }
    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
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

static inline uint8_t* get_map_entry(Esp32s3IntMatrixState* s, hwaddr addr)
{
    int source_index = addr / sizeof(uint32_t);
    if (source_index > ESP32S3_INT_MATRIX_INPUTS * ESP32S3_CPU_COUNT) {
#if INTC_DEBUG
        info_report("%s: source_index %d out of range", __func__, source_index);
#endif // INTC_DEBUG
        return NULL;
    }
    int cpu_index = source_index / ESP32S3_INT_MATRIX_INPUTS;
    source_index = source_index % ESP32S3_INT_MATRIX_INPUTS;
    return &IRQ_MAP(cpu_index, source_index);
}

/* Index of the INTR_STATUS register at this address (either CPU), or -1 */
static int get_status_word(hwaddr addr)
{
    const hwaddr block = ESP32S3_INT_MATRIX_INPUTS * sizeof(uint32_t);
    const hwaddr offset = addr % block - ESP32S3_INT_MATRIX_STATUS_OFFSET;
    if (offset >= ESP32S3_INT_MATRIX_STATUS_WORDS * sizeof(uint32_t)) {
        return -1;
    }
    return offset / sizeof(uint32_t);
}

static uint64_t esp32s3_intmatrix_read(void* opaque, hwaddr addr, unsigned int size)
{
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(opaque);
    const int word = get_status_word(addr);
    if (word >= 0) {
        /* Raw levels of sources 32 * word + 0..31, the same for both CPUs */
        uint32_t status = 0;
        for (int bit = 0; bit < 32; ++bit) {
            if (test_bit(word * 32 + bit, s->irq_levels)) {
                status |= BIT(bit);
            }
        }
        return status;
    }
    uint8_t* map_entry = get_map_entry(s, addr);
    return (map_entry != NULL) ? *map_entry : 0;
}

static void esp32s3_intmatrix_write(void* opaque, hwaddr addr, uint64_t value, unsigned int size)
{
#if INTC_DEBUG
    info_report("\x1b[31m[INTC] esp32s3_intmatrix_write  addr = %ld, value=%ld\x1b[0m", addr, value);
#endif // INTC_DEBUG
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(opaque);
    if (get_status_word(addr) >= 0) {
        /* INTR_STATUS registers are read-only */
        return;
    }
    uint8_t* map_entry = get_map_entry(s, addr);
    if (map_entry != NULL) {
        *map_entry = value & 0x1f;
    }
}

static const MemoryRegionOps esp_intmatrix_ops = {
    .read =  esp32s3_intmatrix_read,
    .write = esp32s3_intmatrix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_intmatrix_reset_hold(Object *obj, ResetType type)
{
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(obj);
    memset(s->irq_map, INTMATRIX_UNINT_VALUE, sizeof(s->irq_map));
    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
        if (s->outputs[i] == NULL) {
            continue;
        }
        for (int int_index = 0; int_index < s->cpu[i]->env.config->nextint; ++int_index) {
            qemu_irq_lower(s->outputs[i][int_index]);
        }
    }
}

static void esp32s3_intmatrix_realize(DeviceState *dev, Error **errp)
{
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(dev);

    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
        if (s->cpu[i]) {
            s->outputs[i] = xtensa_get_extints(&s->cpu[i]->env);
        }
    }
    esp32s3_intmatrix_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
}

static void esp32s3_intmatrix_init(Object *obj)
{
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_intmatrix_ops, s,
                          TYPE_ESP32S3_INTMATRIX, ESP32S3_INT_MATRIX_INPUTS * ESP32S3_CPU_COUNT * sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(DEVICE(s), esp32s3_intmatrix_irq_handler, ESP32S3_INT_MATRIX_INPUTS);
}

static Property esp32s3_intmatrix_properties[] = {
    DEFINE_PROP_LINK("cpu0", Esp32s3IntMatrixState, cpu[0], TYPE_XTENSA_CPU, XtensaCPU *),
    DEFINE_PROP_LINK("cpu1", Esp32s3IntMatrixState, cpu[1], TYPE_XTENSA_CPU, XtensaCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_intmatrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32s3_intmatrix_reset_hold;
    dc->realize = esp32s3_intmatrix_realize;
    device_class_set_props(dc, esp32s3_intmatrix_properties);
}

static const TypeInfo esp32s3_intmatrix_info = {
    .name = TYPE_ESP32S3_INTMATRIX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3IntMatrixState),
    .instance_init = esp32s3_intmatrix_init,
    .class_init = esp32s3_intmatrix_class_init
};

static void esp32s3_intmatrix_register_types(void)
{
    type_register_static(&esp32s3_intmatrix_info);
}

type_init(esp32s3_intmatrix_register_types)

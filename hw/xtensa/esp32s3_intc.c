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

/*
 * The matrix is combinational: a CPU interrupt line is the OR of every source
 * currently routed to it. So the level of each source is kept, and a line is
 * recomputed whenever something that feeds it changes: a source's level, or a
 * mapping that routes a source to it or away from it.
 */
static bool esp32s3_intmatrix_line_level(Esp32s3IntMatrixState *s, int cpu,
                                         int line)
{
    for (int n = 0; n < ESP32S3_INT_MATRIX_INPUTS; ++n) {
        if (IRQ_MAP(cpu, n) == line && test_bit(n, s->irq_levels)) {
            return true;
        }
    }
    return false;
}

static void esp32s3_intmatrix_update_line(Esp32s3IntMatrixState *s, int cpu,
                                          int line)
{
    if (s->outputs[cpu] == NULL) {
        return;
    }
    const XtensaConfig *config = s->cpu[cpu]->env.config;
    for (int int_index = 0; int_index < config->nextint; ++int_index) {
        if (config->extint[int_index] != line) {
            continue;
        }
        const bool level = esp32s3_intmatrix_line_level(s, cpu, line);
        const bool former = (s->out_levels[cpu] & BIT(line)) != 0;
        /*
         * Only a change is forwarded: re-raising an edge-triggered CPU
         * interrupt would inject an edge the hardware never produced.
         */
        if (level != former) {
            s->out_levels[cpu] ^= BIT(line);
            qemu_set_irq(s->outputs[cpu][int_index], level);
        }
        return;
    }
    /*
     * Not an external interrupt of this CPU (e.g. 6, used to disconnect a
     * source): nothing is wired to it.
     */
}

static void esp32s3_intmatrix_irq_handler(void *opaque, int n, int level)
{
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(opaque);

    assert(n < ESP32S3_INT_MATRIX_INPUTS);
    if (level) {
        set_bit(n, s->irq_levels);
    } else {
        clear_bit(n, s->irq_levels);
    }
    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
        esp32s3_intmatrix_update_line(s, i, IRQ_MAP(i, n));
    }
}

static inline uint8_t *get_map_entry(Esp32s3IntMatrixState *s, hwaddr addr,
                                     int *cpu, int *source)
{
    int source_index = addr / sizeof(uint32_t);
    if (source_index >= ESP32S3_INT_MATRIX_INPUTS * ESP32S3_CPU_COUNT) {
#if INTC_DEBUG
        info_report("%s: source_index %d out of range", __func__, source_index);
#endif // INTC_DEBUG
        return NULL;
    }
    *cpu = source_index / ESP32S3_INT_MATRIX_INPUTS;
    *source = source_index % ESP32S3_INT_MATRIX_INPUTS;
    return &IRQ_MAP(*cpu, *source);
}

static uint64_t esp32s3_intmatrix_read(void* opaque, hwaddr addr, unsigned int size)
{
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(opaque);
    int cpu, source;
    uint8_t *map_entry = get_map_entry(s, addr, &cpu, &source);
    return (map_entry != NULL) ? *map_entry : 0;
}

static void esp32s3_intmatrix_write(void* opaque, hwaddr addr, uint64_t value, unsigned int size)
{
#if INTC_DEBUG
    info_report("\x1b[31m[INTC] esp32s3_intmatrix_write  addr = %ld, value=%ld\x1b[0m", addr, value);
#endif // INTC_DEBUG
    Esp32s3IntMatrixState *s = ESP32S3_INTMATRIX(opaque);
    int cpu, source;
    uint8_t *map_entry = get_map_entry(s, addr, &cpu, &source);
    if (map_entry == NULL) {
        return;
    }
    const uint8_t old_line = *map_entry;
    const uint8_t new_line = value & 0x1f;
    *map_entry = new_line;
    if (old_line != new_line) {
        /* The source leaves the old line and starts feeding the new one. */
        esp32s3_intmatrix_update_line(s, cpu, old_line);
        esp32s3_intmatrix_update_line(s, cpu, new_line);
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
    /*
     * This reset comes with the reset of the peripherals, and not all of their
     * models lower their interrupt on reset. On silicon no source is asserted
     * after a system reset. A CPU reset does not get here, and keeps the levels.
     */
    bitmap_zero(s->irq_levels, ESP32S3_INT_MATRIX_INPUTS);
    memset(s->irq_map, INTMATRIX_UNINT_VALUE, sizeof(s->irq_map));
    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
        s->out_levels[i] = 0;
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

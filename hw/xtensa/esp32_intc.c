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

/*
 * The matrix is combinational: a CPU interrupt line is the OR of every source
 * currently routed to it. So the level of each source is kept, and a line is
 * recomputed whenever something that feeds it changes: a source's level, or a
 * mapping that routes a source to it or away from it.
 */
static bool esp32_intmatrix_line_level(Esp32IntMatrixState *s, int cpu,
                                       int line)
{
    for (int n = 0; n < ESP32_INT_MATRIX_INPUTS; ++n) {
        if (IRQ_MAP(cpu, n) == line && test_bit(n, s->irq_levels)) {
            return true;
        }
    }
    return false;
}

static void esp32_intmatrix_update_line(Esp32IntMatrixState *s, int cpu,
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
        const bool level = esp32_intmatrix_line_level(s, cpu, line);
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

static void esp32_intmatrix_irq_handler(void *opaque, int n, int level)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);

    assert(n < ESP32_INT_MATRIX_INPUTS);
    if (level) {
        set_bit(n, s->irq_levels);
    } else {
        clear_bit(n, s->irq_levels);
    }
    for (int i = 0; i < ESP32_CPU_COUNT; ++i) {
        esp32_intmatrix_update_line(s, i, IRQ_MAP(i, n));
    }
}

static inline uint8_t *get_map_entry(Esp32IntMatrixState *s, hwaddr addr,
                                     int *cpu, int *source)
{
    int source_index = addr / sizeof(uint32_t);
    if (source_index >= ESP32_INT_MATRIX_INPUTS * ESP32_CPU_COUNT) {
        error_report("%s: source_index %d out of range", __func__, source_index);
        return NULL;
    }
    *cpu = source_index / ESP32_INT_MATRIX_INPUTS;
    *source = source_index % ESP32_INT_MATRIX_INPUTS;
    return &IRQ_MAP(*cpu, *source);
}

static uint64_t esp32_intmatrix_read(void* opaque, hwaddr addr, unsigned int size)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    int cpu, source;
    uint8_t *map_entry = get_map_entry(s, addr, &cpu, &source);
    return (map_entry != NULL) ? *map_entry : 0;
}

static void esp32_intmatrix_write(void* opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
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
        esp32_intmatrix_update_line(s, cpu, old_line);
        esp32_intmatrix_update_line(s, cpu, new_line);
    }
}

/*
 * DPORT_PRO_INTR_STATUS_0..2 are followed by DPORT_APP_INTR_STATUS_0..2. Each
 * bit is the raw level of source 32 * word + bit, the same for both CPUs.
 */
static uint64_t esp32_intmatrix_status_read(void *opaque, hwaddr addr,
                                            unsigned int size)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(opaque);
    const int word = (addr / sizeof(uint32_t)) % ESP32_INT_MATRIX_STATUS_WORDS;
    uint32_t status = 0;
    for (int bit = 0; bit < 32; ++bit) {
        const int source = word * 32 + bit;
        if (source < ESP32_INT_MATRIX_INPUTS && test_bit(source, s->irq_levels)) {
            status |= BIT(bit);
        }
    }
    return status;
}

static void esp32_intmatrix_status_write(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned int size)
{
    /* INTR_STATUS registers are read-only */
}

static const MemoryRegionOps esp_intmatrix_status_ops = {
    .read =  esp32_intmatrix_status_read,
    .write = esp32_intmatrix_status_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps esp_intmatrix_ops = {
    .read =  esp32_intmatrix_read,
    .write = esp32_intmatrix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_intmatrix_reset_hold(Object *obj, ResetType type)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(obj);
    /*
     * This reset comes with the reset of the peripherals, and not all of their
     * models lower their interrupt on reset. On silicon no source is asserted
     * after a system reset. A CPU reset does not get here, and keeps the levels.
     */
    bitmap_zero(s->irq_levels, ESP32_INT_MATRIX_INPUTS);
    memset(s->irq_map, INTMATRIX_UNINT_VALUE, sizeof(s->irq_map));
    for (int i = 0; i < ESP32_CPU_COUNT; ++i) {
        s->out_levels[i] = 0;
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
    esp32_intmatrix_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
}

static void esp32_intmatrix_init(Object *obj)
{
    Esp32IntMatrixState *s = ESP32_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_intmatrix_ops, s,
                          TYPE_ESP32_INTMATRIX, ESP32_INT_MATRIX_INPUTS * ESP32_CPU_COUNT * sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);
    memory_region_init_io(&s->status_iomem, obj, &esp_intmatrix_status_ops, s,
                          TYPE_ESP32_INTMATRIX ".status",
                          ESP32_CPU_COUNT * ESP32_INT_MATRIX_STATUS_WORDS *
                          sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->status_iomem);

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
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32_intmatrix_reset_hold;
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

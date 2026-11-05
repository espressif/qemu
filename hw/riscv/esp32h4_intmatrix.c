/*
 * ESP32-H4 Interrupt Matrix + CLIC
 *
 * Region 0 — interrupt source-to-CPU-line mapping (DR_REG_INTMTX_BASE)
 * Region 1 — CLIC config/ctrl registers (DR_REG_CLIC_BASE)
 *
 * Forked from esp32c5_intmatrix.c with H4-specific interrupt count (66).
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
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
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/esp32h4_intmatrix.h"
#include "esp_cpu.h"

#define BIT_SET(reg, bit)    ((reg) & BIT(bit))

#define LEVELS_SET(a, b)     ((a)[(b) / 64] &  (1ULL << ((b) % 64)))
#define LEVELS_RAISE(a, b)   do { (a)[(b) / 64] |=  (1ULL << ((b) % 64)); } while (0)
#define LEVELS_LOWER(a, b)   do { (a)[(b) / 64] &= ~(1ULL << ((b) % 64)); } while (0)

static int get_output_line_level(ESP32H4IntMatrixState *s, int line)
{
    for (int i = 0; i < ESP32H4_INT_MATRIX_INPUTS; i++) {
        if (s->irq_map[i] == line && LEVELS_SET(s->irq_levels, i)) {
            return 1;
        }
    }
    return 0;
}

static bool line_should_assert(ESP32H4IntMatrixState *s, int line)
{
    if (line == 0 || line > ESP32H4_CPU_INT_COUNT) {
        return false;
    }
    return BIT_SET(s->irq_enabled, line) &&
           get_output_line_level(s, line) != 0 &&
           (s->irq_prio[line] >= s->irq_thres);
}

static void update_line(ESP32H4IntMatrixState *s, int line)
{
    if (line == 0 || line > ESP32H4_CPU_INT_COUNT) {
        return;
    }
    if (line_should_assert(s, line)) {
        qemu_irq_raise(s->out_irqs[line]);
    } else {
        qemu_irq_lower(s->out_irqs[line]);
    }
}

static void refresh_all(ESP32H4IntMatrixState *s)
{
    for (uint32_t i = 1; i <= ESP32H4_CPU_INT_COUNT; i++) {
        update_line(s, i);
    }
}

static void irq_handler(void *opaque, int n, int level)
{
    ESP32H4IntMatrixState *s = ESP32H4_INTMATRIX(opaque);
    assert(n < ESP32H4_INT_MATRIX_INPUTS);

    level = level ? 1 : 0;
    const int former_level = LEVELS_SET(s->irq_levels, n) ? 1 : 0;

    if (level) {
        LEVELS_RAISE(s->irq_levels, n);
    } else {
        LEVELS_LOWER(s->irq_levels, n);
    }

    if (former_level != level) {
        const int line = s->irq_map[n];
        update_line(s, line);
    }
}

/* Region 0 — Interrupt mapping registers */

static uint64_t map_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H4IntMatrixState *s = ESP32H4_INTMATRIX(opaque);
    const uint32_t idx = addr / sizeof(uint32_t);

    if (idx < ESP32H4_INT_MATRIX_INPUTS) {
        return s->irq_map[idx];
    }
    return 0;
}

static void map_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned int size)
{
    ESP32H4IntMatrixState *s = ESP32H4_INTMATRIX(opaque);
    const uint32_t idx = addr / sizeof(uint32_t);

    if (idx < ESP32H4_INT_MATRIX_INPUTS) {
        const uint8_t old_line = s->irq_map[idx];
        const uint8_t new_line = value & 0x3f;
        s->irq_map[idx] = new_line;
        if (old_line != new_line) {
            update_line(s, old_line);
            update_line(s, new_line);
        }
    }
}

static const MemoryRegionOps map_ops = {
    .read  = map_read,
    .write = map_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* Region 1 — CLIC registers */

#define CLIC_CONFIG_OFF   0x000
#define CLIC_INFO_OFF     0x004
#define CLIC_THRESH_OFF   0x008
#define CLIC_CTRL_OFF     0x1000

static uint64_t clic_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H4IntMatrixState *s = ESP32H4_INTMATRIX(opaque);

    if (addr == CLIC_CONFIG_OFF) {
        return s->clic_config;
    } else if (addr == CLIC_INFO_OFF) {
        return (3 << 21) | ESP32H4_CLIC_NUM_INT;
    } else if (addr == CLIC_THRESH_OFF) {
        return (uint32_t)s->irq_thres << 24;
    } else if (addr >= CLIC_CTRL_OFF &&
               addr < CLIC_CTRL_OFF + ESP32H4_CLIC_NUM_INT * 4) {
        uint32_t line = (addr - CLIC_CTRL_OFF) / 4;
        if (line < ESP32H4_CLIC_NUM_INT) {
            uint32_t val = s->clic_ctrl[line];
            if (line <= ESP32H4_CPU_INT_COUNT) {
                if (s->irq_enabled & BIT(line)) {
                    val |= (1 << 8);
                }
                val = (val & ~(0xFFu << 24)) |
                      ((uint32_t)s->irq_prio[line] << 24);
                if (get_output_line_level(s, line)) {
                    val |= 1;
                }
            }
            return val;
        }
    }
    return 0;
}

static void clic_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned int size)
{
    ESP32H4IntMatrixState *s = ESP32H4_INTMATRIX(opaque);

    if (addr == CLIC_CONFIG_OFF) {
        s->clic_config = value;
    } else if (addr == CLIC_THRESH_OFF) {
        const uint8_t prio = (value >> 24) & 0xff;
        if (prio != s->irq_thres) {
            s->irq_thres = prio;
            refresh_all(s);
        }
    } else if (addr >= CLIC_CTRL_OFF &&
               addr < CLIC_CTRL_OFF + ESP32H4_CLIC_NUM_INT * 4) {
        const uint32_t base = addr & ~3u;
        const uint32_t line = (base - CLIC_CTRL_OFF) / 4;
        if (line < ESP32H4_CLIC_NUM_INT && line <= ESP32H4_CPU_INT_COUNT) {
            uint32_t cur = s->clic_ctrl[line];
            const uint32_t byte_off = addr & 3;
            for (unsigned i = 0; i < size; i++) {
                const uint32_t shift = (byte_off + i) * 8;
                cur &= ~(0xFFu << shift);
                cur |= ((uint32_t)((value >> (i * 8)) & 0xFF)) << shift;
            }
            s->clic_ctrl[line] = cur;

            const uint8_t ie = (cur >> 8) & 1;
            if (ie) {
                s->irq_enabled |= BIT(line);
            } else {
                s->irq_enabled &= ~BIT(line);
            }
            s->irq_prio[line] = (cur >> 24) & 0xff;
            esp_cpu_clic_set_line_ctl(s->cpu, line, s->irq_prio[line]);

            update_line(s, line);
        }
    }
}

static const MemoryRegionOps clic_ops = {
    .read  = clic_read,
    .write = clic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void esp32h4_intmatrix_reset_hold(Object *obj, ResetType type)
{
    ESP32H4IntMatrixState *s = ESP32H4_INTMATRIX(obj);

    memset(s->irq_map, 0, sizeof(s->irq_map));
    memset(s->irq_prio, 0, sizeof(s->irq_prio));
    memset(s->clic_ctrl, 0, sizeof(s->clic_ctrl));
    s->clic_config = 0;
    s->irq_thres = 0;
    s->irq_levels[0] = 0;
    s->irq_levels[1] = 0;
    s->irq_trigger = 0;
    s->irq_enabled = 0;
    for (int i = 0; i <= ESP32H4_CPU_INT_COUNT; i++) {
        qemu_irq_lower(s->out_irqs[i]);
    }

    if (s->cpu) {
        riscv_csr_write(&s->cpu->parent_obj.env, CSR_MIE, 0xFFFFFFFFu);
    }
}

static void esp32h4_intmatrix_realize(DeviceState *dev, Error **errp)
{
    esp32h4_intmatrix_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
}

static void esp32h4_intmatrix_init(Object *obj)
{
    ESP32H4IntMatrixState *s = ESP32H4_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->map_iomem, obj, &map_ops, s,
                          TYPE_ESP32H4_INTMATRIX ".map",
                          ESP32H4_INT_MATRIX_MAP_IO_SIZE);
    sysbus_init_mmio(sbd, &s->map_iomem);

    memory_region_init_io(&s->clic_iomem, obj, &clic_ops, s,
                          TYPE_ESP32H4_INTMATRIX ".clic",
                          ESP32H4_CLIC_IO_SIZE);
    sysbus_init_mmio(sbd, &s->clic_iomem);

    qdev_init_gpio_in(DEVICE(s), irq_handler, ESP32H4_INT_MATRIX_INPUTS);
    qdev_init_gpio_out_named(DEVICE(s), s->out_irqs,
                             ESP32H4_INT_MATRIX_OUTPUT_NAME,
                             ESP32H4_CPU_INT_COUNT + 1);
}

static Property esp32h4_intmatrix_props[] = {
    DEFINE_PROP_LINK("cpu", ESP32H4IntMatrixState, cpu,
                     TYPE_ESP_RISCV_CPU, EspRISCVCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32h4_intmatrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32h4_intmatrix_reset_hold;
    dc->realize = esp32h4_intmatrix_realize;
    device_class_set_props(dc, esp32h4_intmatrix_props);
}

static const TypeInfo esp32h4_intmatrix_info = {
    .name          = TYPE_ESP32H4_INTMATRIX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H4IntMatrixState),
    .instance_init = esp32h4_intmatrix_init,
    .class_init    = esp32h4_intmatrix_class_init,
};

static void esp32h4_intmatrix_register_types(void)
{
    type_register_static(&esp32h4_intmatrix_info);
}

type_init(esp32h4_intmatrix_register_types)

/*
 * ESP32-C5 Interrupt Matrix + CLIC
 *
 * Region 0 — interrupt source-to-CPU-line mapping (DR_REG_INTMTX_BASE)
 * Region 1 — CLIC config/ctrl registers (DR_REG_CLIC_BASE)
 *
 * Modelled with the same level-triggered approach as the C6 intmatrix:
 *   line_should_assert() consults the source level, the per-line CLIC
 *   IE byte (mirrored in `irq_enabled`), and the CLIC priority threshold;
 *   update_line() raises or lowers the matching CPU IRQ accordingly.
 *
 * Unlike the C6/H2 PLIC SOCs, the ESP32-C5 does NOT repurpose the mie CSR
 * as a per-line bitmap (CLIC has its own enable byte), so this device does
 * not need to register a mie-changed callback on the CPU.
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
#include "hw/riscv/esp32c5_intmatrix.h"
#include "esp_cpu.h"

#define BIT_SET(reg, bit)    ((reg) & BIT(bit))
#define CLEAR_BIT(reg, bit)  do { (reg) &= ~BIT(bit); } while (0)
#define SET_BIT(reg, bit)    do { (reg) |= BIT(bit); } while (0)

#define LEVELS_SET(a, b)     ((a)[(b) / 64] &  (1ULL << ((b) % 64)))
#define LEVELS_RAISE(a, b)   do { (a)[(b) / 64] |=  (1ULL << ((b) % 64)); } while (0)
#define LEVELS_LOWER(a, b)   do { (a)[(b) / 64] &= ~(1ULL << ((b) % 64)); } while (0)

/* ------------------------------------------------------------------ */
/*  Core interrupt logic                                               */
/* ------------------------------------------------------------------ */

static int get_output_line_level(ESP32C5IntMatrixState *s, int line)
{
    for (int i = 0; i < ESP32C5_INT_MATRIX_INPUTS; i++) {
        if (s->irq_map[i] == line && LEVELS_SET(s->irq_levels, i)) {
            return 1;
        }
    }
    return 0;
}

static bool line_should_assert(ESP32C5IntMatrixState *s, int line)
{
    if (line == 0 || line > ESP32C5_CPU_INT_COUNT) {
        return false;
    }
    return BIT_SET(s->irq_enabled, line) &&
           get_output_line_level(s, line) != 0 &&
           (s->irq_prio[line] >= s->irq_thres);
}

static void update_line(ESP32C5IntMatrixState *s, int line)
{
    if (line == 0 || line > ESP32C5_CPU_INT_COUNT) {
        return;
    }
    if (line_should_assert(s, line)) {
        qemu_irq_raise(s->out_irqs[line]);
    } else {
        qemu_irq_lower(s->out_irqs[line]);
    }
}

static void refresh_all(ESP32C5IntMatrixState *s)
{
    for (uint32_t i = 1; i <= ESP32C5_CPU_INT_COUNT; i++) {
        update_line(s, i);
    }
}

/* ------------------------------------------------------------------ */
/*  GPIO input handler (peripheral sources)                            */
/* ------------------------------------------------------------------ */

static void irq_handler(void *opaque, int n, int level)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(opaque);
    assert(n < ESP32C5_INT_MATRIX_INPUTS);

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

/* ------------------------------------------------------------------ */
/*  Region 0 — Interrupt mapping registers                             */
/* ------------------------------------------------------------------ */

#define MAP_STATUS_START (0x150 / sizeof(uint32_t))
#define MAP_STATUS_END   (0x15c / sizeof(uint32_t))

static uint64_t map_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(opaque);
    const uint32_t idx = addr / sizeof(uint32_t);

    if (idx < ESP32C5_INT_MATRIX_INPUTS) {
        return s->irq_map[idx];
    } else if (idx >= MAP_STATUS_START && idx < MAP_STATUS_END) {
        /* Raw source levels, whatever the mapping. Read-only. */
        const uint32_t word = idx - MAP_STATUS_START;
        return (uint32_t)(s->irq_levels[word / 2] >> ((word % 2) * 32));
    }
    return 0;
}

static void map_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned int size)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(opaque);
    const uint32_t idx = addr / sizeof(uint32_t);

    if (idx < ESP32C5_INT_MATRIX_INPUTS) {
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

/* ------------------------------------------------------------------ */
/*  Region 1 — CLIC registers                                          */
/* ------------------------------------------------------------------ */
/* Layout (relative to DR_REG_CLIC_BASE = 0x20800000):
 *   0x000: CLIC_INT_CONFIG_REG
 *   0x004: CLIC_INT_INFO_REG
 *   0x008: CLIC_INT_THRESH_REG (threshold in bits [31:24])
 *   0x1000 + i*4: per-interrupt control (IP[0], IE[8], ATTR[22:16], CTL[31:24])
 */
#define CLIC_CONFIG_OFF   0x000
#define CLIC_INFO_OFF     0x004
#define CLIC_THRESH_OFF   0x008
#define CLIC_CTRL_OFF     0x1000

static uint64_t clic_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(opaque);

    if (addr == CLIC_CONFIG_OFF) {
        return s->clic_config;
    } else if (addr == CLIC_INFO_OFF) {
        return (3 << 21) | ESP32C5_CLIC_NUM_INT;
    } else if (addr == CLIC_THRESH_OFF) {
        return (uint32_t)s->irq_thres << 24;
    } else if (addr >= CLIC_CTRL_OFF &&
               addr < CLIC_CTRL_OFF + ESP32C5_CLIC_NUM_INT * 4) {
        uint32_t line = (addr - CLIC_CTRL_OFF) / 4;
        if (line < ESP32C5_CLIC_NUM_INT) {
            uint32_t val = s->clic_ctrl[line];
            if (line <= ESP32C5_CPU_INT_COUNT) {
                if (s->irq_enabled & BIT(line)) {
                    val |= (1 << 8);
                }
                val = (val & ~(0xFFu << 24)) |
                      ((uint32_t)s->irq_prio[line] << 24);
                /* IP byte 0: reflect current source level (independent of
                 * enable / threshold), matching CLIC pending semantics. */
                if (get_output_line_level(s, line)) {
                    val |= 1;
                }
            }
            /* The model only sees the addressed bytes, not an aligned word */
            return val >> ((addr & 3) * 8);
        }
    }
    return 0;
}

static void clic_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned int size)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(opaque);

    if (addr == CLIC_CONFIG_OFF) {
        s->clic_config = value;
    } else if (addr == CLIC_THRESH_OFF) {
        const uint8_t prio = (value >> 24) & 0xff;
        if (prio != s->irq_thres) {
            s->irq_thres = prio;
            refresh_all(s);
        }
    } else if (addr >= CLIC_CTRL_OFF &&
               addr < CLIC_CTRL_OFF + ESP32C5_CLIC_NUM_INT * 4) {
        const uint32_t base = addr & ~3u;
        const uint32_t line = (base - CLIC_CTRL_OFF) / 4;
        if (line < ESP32C5_CLIC_NUM_INT && line <= ESP32C5_CPU_INT_COUNT) {
            /*
             * The ROM writes individual bytes (sb) to CLIC entries:
             *   byte 0 = IP, byte 1 = IE, byte 2 = ATTR, byte 3 = CTL/prio
             * Merge sub-word writes into the full 32-bit control word.
             */
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
            esp_cpu_clic_set_line(s->cpu, line, (cur >> 16) & 0xff, s->irq_prio[line]);

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

/* ------------------------------------------------------------------ */
/*  QOM boilerplate                                                    */
/* ------------------------------------------------------------------ */

static void esp32c5_intmatrix_reset_hold(Object *obj, ResetType type)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(obj);

    memset(s->irq_map, 0, sizeof(s->irq_map));
    memset(s->irq_prio, 0, sizeof(s->irq_prio));
    memset(s->clic_ctrl, 0, sizeof(s->clic_ctrl));
    s->clic_config = 0;
    s->irq_thres = 0;
    s->irq_levels[0] = 0;
    s->irq_levels[1] = 0;
    s->irq_trigger = 0;
    s->irq_enabled = 0;
    for (int i = 0; i <= ESP32C5_CPU_INT_COUNT; i++) {
        qemu_irq_lower(s->out_irqs[i]);
    }

    /* Mirror the C6/H2 reset behaviour: preload mie so direct-loaded images
     * (BIST, single-binary tests) start with external IRQs unmasked even
     * if they skip the BootROM. */
    if (s->cpu) {
        riscv_csr_write(&s->cpu->parent_obj.env, CSR_MIE, 0xFFFFFFFFu);
    }
}

static void esp32c5_intmatrix_realize(DeviceState *dev, Error **errp)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(dev);
    esp32c5_intmatrix_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
    /* No mie-changed callback needed: the C5 uses CLIC IE bytes (mirrored
     * in `irq_enabled`) for per-line gating; mie is not repurposed. */
    (void)s;
}

static void esp32c5_intmatrix_init(Object *obj)
{
    ESP32C5IntMatrixState *s = ESP32C5_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->map_iomem, obj, &map_ops, s,
                          TYPE_ESP32C5_INTMATRIX ".map",
                          ESP32C5_INT_MATRIX_MAP_IO_SIZE);
    sysbus_init_mmio(sbd, &s->map_iomem);

    memory_region_init_io(&s->clic_iomem, obj, &clic_ops, s,
                          TYPE_ESP32C5_INTMATRIX ".clic",
                          ESP32C5_CLIC_IO_SIZE);
    sysbus_init_mmio(sbd, &s->clic_iomem);

    qdev_init_gpio_in(DEVICE(s), irq_handler, ESP32C5_INT_MATRIX_INPUTS);
    qdev_init_gpio_out_named(DEVICE(s), s->out_irqs,
                             ESP32C5_INT_MATRIX_OUTPUT_NAME,
                             ESP32C5_CPU_INT_COUNT + 1);
}

static Property esp32c5_intmatrix_props[] = {
    DEFINE_PROP_LINK("cpu", ESP32C5IntMatrixState, cpu,
                     TYPE_ESP_RISCV_CPU, EspRISCVCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c5_intmatrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c5_intmatrix_reset_hold;
    dc->realize = esp32c5_intmatrix_realize;
    device_class_set_props(dc, esp32c5_intmatrix_props);
}

static const TypeInfo esp32c5_intmatrix_info = {
    .name          = TYPE_ESP32C5_INTMATRIX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5IntMatrixState),
    .instance_init = esp32c5_intmatrix_init,
    .class_init    = esp32c5_intmatrix_class_init,
};

static void esp32c5_intmatrix_register_types(void)
{
    type_register_static(&esp32c5_intmatrix_info);
}

type_init(esp32c5_intmatrix_register_types)

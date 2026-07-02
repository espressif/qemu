/*
 * ESP32-C6 Interrupt Matrix + PLIC
 *
 * Region 0 — interrupt source-to-CPU-line mapping (DR_REG_INTMTX_BASE)
 * Region 1 — PLIC enable / priority / threshold  (DR_REG_PLIC_MX_BASE)
 *
 * Based on the ESP32-C3 interrupt matrix; adapted for the C6's 77
 * peripheral sources and separate PLIC register block.
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
#include "hw/riscv/esp32c6_intmatrix.h"
#include "esp_cpu.h"

#define INTMATRIX_DEBUG   0
#define INTMATRIX_WARNING 0

#define BIT_SET(reg, bit)    ((reg) & BIT(bit))
#define CLEAR_BIT(reg, bit)  do { (reg) &= ~BIT(bit); } while (0)
#define SET_BIT(reg, bit)    do { (reg) |= BIT(bit); } while (0)

#define LEVELS_SET(a, b)     ((a)[(b) / 64] &  (1ULL << ((b) % 64)))
#define LEVELS_RAISE(a, b)   do { (a)[(b) / 64] |=  (1ULL << ((b) % 64)); } while (0)
#define LEVELS_LOWER(a, b)   do { (a)[(b) / 64] &= ~(1ULL << ((b) % 64)); } while (0)

/* ------------------------------------------------------------------ */
/*  Core interrupt logic                                               */
/* ------------------------------------------------------------------ */

static int get_output_line_level(ESP32C6IntMatrixState *s, int line)
{
    for (int i = 0; i < ESP32C6_INT_MATRIX_INPUTS; i++) {
        if (s->irq_map[i] == line && LEVELS_SET(s->irq_levels, i)) {
            return 1;
        }
    }
    return 0;
}

static bool line_should_assert(ESP32C6IntMatrixState *s, int line)
{
    if (line == 0) {
        return false;
    }

    /* The C6 has TWO independent per-line enable gates that must both be
     * open (TRM §1.6.2 "Enable State"):
     *   - PLIC_MXINT_ENABLE_REG (modelled by `irq_enabled`)
     *   - the corresponding MXIE bit in the mie CSR (modelled by
     *     `cpu->mie_enabled`, populated by guest writes through our custom
     *     mie CSR ops in target/riscv/esp_cpu.c)
     */
    return BIT_SET(s->irq_enabled, line) &&
           BIT_SET(s->cpu->mie_enabled, line) &&
           get_output_line_level(s, line) != 0 &&
           (s->irq_prio[line] >= s->irq_thres);
}

static void update_line(ESP32C6IntMatrixState *s, int line)
{
    if (line == 0) {
        return;
    }

    if (line_should_assert(s, line)) {
        qemu_irq_raise(s->out_irqs[line]);
    } else {
        qemu_irq_lower(s->out_irqs[line]);
    }
}

static void refresh_all(ESP32C6IntMatrixState *s)
{
    for (uint32_t i = 1; i <= ESP32C6_CPU_INT_COUNT; i++) {
        update_line(s, i);
    }
}

/* ------------------------------------------------------------------ */
/*  GPIO input handler (peripheral sources)                            */
/* ------------------------------------------------------------------ */

static void irq_handler(void *opaque, int n, int level)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(opaque);
    assert(n < ESP32C6_INT_MATRIX_INPUTS);

    level = level ? 1 : 0;
    const int former_level = LEVELS_SET(s->irq_levels, n) ? 1 : 0;

    if (level) {
        LEVELS_RAISE(s->irq_levels, n);
    } else {
        LEVELS_LOWER(s->irq_levels, n);
    }

    /* Nothing to do if the level is unchanged. */
    if (former_level != level) {
        const int line = s->irq_map[n];
        update_line(s, line);
    }
}

/* ------------------------------------------------------------------ */
/*  Region 0 — Interrupt mapping registers                             */
/* ------------------------------------------------------------------ */

/* C6 mapping layout mirrors C3: first N words are source→line maps,
 * followed by priority / enable / type / threshold at the same offsets. */
#define MAP_PRIO_START  (0x118 / sizeof(uint32_t))
#define MAP_PRIO_END    (0x190 / sizeof(uint32_t))
#define MAP_ENABLE_REG  (0x104 / sizeof(uint32_t))
#define MAP_TYPE_REG    (0x108 / sizeof(uint32_t))
#define MAP_THRESH_REG  (0x194 / sizeof(uint32_t))

static uint64_t map_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(opaque);
    const uint32_t idx = addr / sizeof(uint32_t);

    if (idx < ESP32C6_INT_MATRIX_INPUTS) {
        return s->irq_map[idx];
    } else if (idx >= MAP_PRIO_START && idx < MAP_PRIO_END) {
        return s->irq_prio[idx - MAP_PRIO_START + 1];
    } else if (idx == MAP_THRESH_REG) {
        return s->irq_thres;
    } else if (idx == MAP_ENABLE_REG) {
        return s->irq_enabled;
    } else if (idx == MAP_TYPE_REG) {
        return 0;
    }
    return 0;
}

static void map_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned int size)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(opaque);
    const uint32_t idx = addr / sizeof(uint32_t);

    if (idx < ESP32C6_INT_MATRIX_INPUTS) {
        const uint8_t old_line = s->irq_map[idx];
        const uint8_t new_line = value & 0x1f;
        s->irq_map[idx] = new_line;
        if (old_line != new_line) {
            update_line(s, old_line);
            update_line(s, new_line);
        }
    } else if (idx >= MAP_PRIO_START && idx < MAP_PRIO_END) {
        const uint32_t line = idx - MAP_PRIO_START + 1;
        s->irq_prio[line] = value & 0xf;
        update_line(s, line);
    } else if (idx == MAP_THRESH_REG) {
        const uint8_t prio = value & 0xf;
        if (prio != s->irq_thres) {
            s->irq_thres = prio;
            refresh_all(s);
        }
    } else if (idx == MAP_ENABLE_REG) {
        const uint64_t prev = s->irq_enabled;
        s->irq_enabled = value;
        for (int i = 0; i <= ESP32C6_CPU_INT_COUNT; i++) {
            if ((value & BIT(i)) != (prev & BIT(i))) {
                update_line(s, i);
            }
        }
    } else if (idx == MAP_TYPE_REG) {
        if (value != 0) {
#if INTMATRIX_WARNING
            warn_report("[C6-INTMATRIX] Edge-triggered interrupts not supported\n");
#endif
        }
    }
}

static const MemoryRegionOps map_ops = {
    .read  = map_read,
    .write = map_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* ------------------------------------------------------------------ */
/*  Region 1 — PLIC MX registers                                      */
/* ------------------------------------------------------------------ */

#define PLIC_ENABLE_OFF   0x00
#define PLIC_TYPE_OFF     0x04
#define PLIC_CLEAR_OFF    0x08
#define PLIC_EIP_OFF      0x0C
#define PLIC_PRI_START    0x10
#define PLIC_PRI_END      0x90   /* 32 × 4 = 0x80; 0x10 + 0x80 = 0x90 */
#define PLIC_THRESH_OFF   0x90
#define PLIC_CLAIM_OFF    0x94

/*
 * Compute the External Interrupt Pending bitmap on demand.  In the new
 * level-triggered model there is no separate "pending" state; a bit in
 * EIP simply reflects whether the corresponding output line is currently
 * asserted (enabled, source level high, priority above threshold).
 */
static uint32_t compute_eip(ESP32C6IntMatrixState *s)
{
    uint32_t eip = 0;
    for (int i = 1; i <= ESP32C6_CPU_INT_COUNT; i++) {
        if (line_should_assert(s, i)) {
            eip |= BIT(i);
        }
    }
    return eip;
}

static uint64_t plic_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(opaque);

    if (addr == PLIC_ENABLE_OFF) {
        return s->irq_enabled;
    } else if (addr == PLIC_EIP_OFF) {
        return compute_eip(s);
    } else if (addr >= PLIC_PRI_START && addr < PLIC_PRI_END) {
        uint32_t line = (addr - PLIC_PRI_START) / 4;
        if (line <= ESP32C6_CPU_INT_COUNT) {
            return s->irq_prio[line];
        }
    } else if (addr == PLIC_THRESH_OFF) {
        return s->irq_thres;
    }
    return 0;
}

static void plic_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned int size)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(opaque);

    if (addr == PLIC_ENABLE_OFF) {
        const uint64_t prev = s->irq_enabled;
        s->irq_enabled = value;
        for (int i = 0; i <= ESP32C6_CPU_INT_COUNT; i++) {
            if ((value & BIT(i)) != (prev & BIT(i))) {
                update_line(s, i);
            }
        }
    } else if (addr >= PLIC_PRI_START && addr < PLIC_PRI_END) {
        uint32_t line = (addr - PLIC_PRI_START) / 4;
        if (line <= ESP32C6_CPU_INT_COUNT) {
            s->irq_prio[line] = value & 0xf;
            update_line(s, line);
        }
    } else if (addr == PLIC_THRESH_OFF) {
        const uint8_t prio = value & 0xf;
        if (prio != s->irq_thres) {
            s->irq_thres = prio;
            refresh_all(s);
        }
    }
}

static const MemoryRegionOps plic_ops = {
    .read  = plic_read,
    .write = plic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* ------------------------------------------------------------------ */
/*  QOM boilerplate                                                    */
/* ------------------------------------------------------------------ */

/*
 * Notifier registered with the CPU so that any guest write to the mie CSR
 * (which goes through our custom mie ops in target/riscv/esp_cpu.c) refreshes
 * the per-line IRQ assertion state — bits in mie can mask or unmask lines
 * independently of PLIC_MXINT_ENABLE_REG.
 */
static void esp32c6_intmatrix_mie_changed(void *opaque)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(opaque);
    refresh_all(s);
}

static void esp32c6_intmatrix_reset_hold(Object *obj, ResetType type)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(obj);

    memset(s->irq_map, 0, sizeof(s->irq_map));
    memset(s->irq_prio, 0, sizeof(s->irq_prio));
    s->irq_thres = 0;
    s->irq_levels[0] = 0;
    s->irq_levels[1] = 0;
    s->irq_trigger = 0;
    s->irq_enabled = 0;
    for (int i = 0; i <= ESP32C6_CPU_INT_COUNT; i++) {
        qemu_irq_lower(s->out_irqs[i]);
    }

    /* The C6 silicon resets `mie` to 0 (TRM Reg 1.8) and the BootROM is
     * expected to program it before handing control to the bootloader/app.
     * To keep direct-loaded images (BIST, single-binary tests, anything that
     * skips the BootROM) on equal footing with BootROM-launched code, we
     * preload `mie` to "all bits enabled" here.  This routes through our
     * custom mie CSR ops, so it also populates `cpu->mie_enabled` and fires
     * a refresh.  Any subsequent guest `csrw mie, …` overrides it. */
    if (s->cpu) {
        riscv_csr_write(&s->cpu->parent_obj.env, CSR_MIE, 0xFFFFFFFFu);
    }
}

static void esp32c6_intmatrix_realize(DeviceState *dev, Error **errp)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(dev);
    if (s->cpu) {
        esp_cpu_set_mie_changed_cb(s->cpu, esp32c6_intmatrix_mie_changed, s);
    }
    esp32c6_intmatrix_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
}

static void esp32c6_intmatrix_init(Object *obj)
{
    ESP32C6IntMatrixState *s = ESP32C6_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->map_iomem, obj, &map_ops, s,
                          TYPE_ESP32C6_INTMATRIX ".map",
                          ESP32C6_INTMATRIX_MAP_IO_SIZE);
    sysbus_init_mmio(sbd, &s->map_iomem);

    memory_region_init_io(&s->plic_iomem, obj, &plic_ops, s,
                          TYPE_ESP32C6_INTMATRIX ".plic",
                          ESP32C6_PLIC_IO_SIZE);
    sysbus_init_mmio(sbd, &s->plic_iomem);

    qdev_init_gpio_in(DEVICE(s), irq_handler, ESP32C6_INT_MATRIX_INPUTS);
    qdev_init_gpio_out_named(DEVICE(s), s->out_irqs,
                             ESP32C6_INT_MATRIX_OUTPUT_NAME,
                             ESP32C6_CPU_INT_COUNT + 1);
}

static Property esp32c6_intmatrix_props[] = {
    DEFINE_PROP_LINK("cpu", ESP32C6IntMatrixState, cpu,
                     TYPE_ESP_RISCV_CPU, EspRISCVCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c6_intmatrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c6_intmatrix_reset_hold;
    dc->realize = esp32c6_intmatrix_realize;
    device_class_set_props(dc, esp32c6_intmatrix_props);
}

static const TypeInfo esp32c6_intmatrix_info = {
    .name          = TYPE_ESP32C6_INTMATRIX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6IntMatrixState),
    .instance_init = esp32c6_intmatrix_init,
    .class_init    = esp32c6_intmatrix_class_init,
};

static void esp32c6_intmatrix_register_types(void)
{
    type_register_static(&esp32c6_intmatrix_info);
}

type_init(esp32c6_intmatrix_register_types)

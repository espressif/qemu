/*
 * ESP32-C3 Interrupt Matrix
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/queue.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/esp32c3_intmatrix.h"
#include "esp_cpu.h"

#define INTMATRIX_DEBUG     0
#define INTMATRIX_WARNING   0

#define BIT_SET(reg, bit)   ((reg) & BIT(bit))
#define CLEAR_BIT(reg, bit) do { (reg) &= ~BIT(bit); } while(0)
#define SET_BIT(reg, bit)   do { (reg) |= BIT(bit); } while(0)


static void esp32c3_do_int(ESP32C3IntMatrixState *s, int line)
{
    qemu_irq_pulse(s->out_irqs[line]);
}


static int esp32c3_get_output_line_level(ESP32C3IntMatrixState *s, int line)
{
    int level_shared = 0;

    for (int i = 0; level_shared == 0 && i < ESP32C3_INT_MATRIX_INPUTS; i++) {
        const uint_fast8_t mapped = s->irq_map[i];
        if (mapped == line && (s->irq_levels & BIT(i)))
        {
            level_shared |= 1;
        }
    }

    return level_shared;
}

/**
 * Try to clear the given interrupt from the pending bitmap.
 * If the signal is shared with other interrupt sources, make sure all of them are low (0)
 * before clearing the pending IRQ from the bitmap.
 */
static void esp32c3_intmatrix_clear_pending(ESP32C3IntMatrixState *s, int line)
{
    /* Check if another GPIO IRQ is sharing the same output line. They must all be low before
     * clearing the pending bit. This is due to the fact that output lines
     * can be shared on the ESP32-C3 */
    const int output_level = esp32c3_get_output_line_level(s, line);
    if (!output_level) {
        /* The output interrupt line is 0, so we can clear the pending flag */
        CLEAR_BIT(s->irq_pending, line);
    }
}


static inline bool esp32c3_intmatrix_can_trigger(ESP32C3IntMatrixState *s)
{
    return esp_cpu_accept_interrupts(s->cpu);
}


static void esp32c3_intmatrix_irq_handler(void *opaque, int n, int level)
{
    ESP32C3IntMatrixState *s = ESP32C3_INTMATRIX(opaque);

    /* Update the level mirror */
    assert(n <= ESP32C3_INT_MATRIX_INPUTS);

    /* Make sure level is 0 or 1 */
    level = level ? 1 : 0;

    /* Save the former level of the pin */
    const int former_level = BIT_SET(s->irq_levels, n) ? 1 : 0;

    if (level) {
        SET_BIT(s->irq_levels, n);
    } else {
        CLEAR_BIT(s->irq_levels, n);
    }

    const int line = s->irq_map[n];

    /* If the line is not enable, don't do anything special, the level has been recorded already.
     * Don't do anything if the line is at the same level as before */
    if ((s->irq_enabled & BIT(line)) == 0 || former_level == level) {
        return;
    }

    /* If the new level is high, check that the priority is equal or bigger than the threshold.
     * If that's the case, we can execute the interrupt, else, mark it as pending. */
    if (level == 1) {
#if INTMATRIX_DEBUG
        info_report("\x1b[31m[INTMATRIX] IRQ %d priority set to %d, CPU threshold %d \x1b[0m\n",
                    line, s->irq_prio[line], s->irq_thres);
#endif

        if (s->irq_prio[line] >= s->irq_thres && esp32c3_intmatrix_can_trigger(s)) {
            esp32c3_do_int(s, line);
        } else {
            SET_BIT(s->irq_pending, line);
        }
    } else if (BIT_SET(s->irq_pending, line)) {
        esp32c3_intmatrix_clear_pending(s, line);
    }
}


static void esp32c3_intmatrix_irq_prio_changed(ESP32C3IntMatrixState* s, uint32_t line, uint8_t priority)
{
    const bool accept = esp32c3_intmatrix_can_trigger(s);

    if (accept && priority >= s->irq_thres && BIT_SET(s->irq_pending, line)) {
        /* No need to clear the pending bit here. As soon as the interrupt source will be ACK by the
         * software, its level will be update, as well as its pending state. */
        esp32c3_do_int(s, line);
    }
}


static void esp32c3_intmatrix_core_prio_changed(ESP32C3IntMatrixState* s, uint64_t new_cpu_priority)
{
    uint64_t pending = s->irq_pending;
    const bool accept = esp32c3_intmatrix_can_trigger(s);

    if (pending && accept) {
        int64_t priority = -1;
        uint_fast32_t line = 0;

        /* Clear all the interrupts that have a lower priority than the new CPU threshold */
        for (uint_fast32_t i = 1; i <= ESP32C3_CPU_INT_COUNT; i++) {

            const uint64_t line_prio = s->irq_prio[i];
            if (line_prio < new_cpu_priority) {
                CLEAR_BIT(pending, i);
            }
        }

        /* No high level interrupt pending? */
        if (pending == 0) {
            return;
        }

        /* Look for the highest priority pending interrupt */
        for (uint_fast32_t i = 1; i <= ESP32C3_CPU_INT_COUNT; i++) {
            const int64_t line_prio = (int64_t) s->irq_prio[i];
            if (BIT_SET(pending, i) && line_prio > priority) {
                priority = line_prio;
                line = i;
            }
        }

        /* Make sure a line was selected with its new priority */
        assert(line != 0);
        assert(priority >= new_cpu_priority);
        /* No need to clear the pending bit here. As soon as the interrupt source will be ACK by the
         * software, its level will be update, as well as its pending state. */
        esp32c3_do_int(s, line);
    }
}


/**
 * Bottom-half: deferred re-evaluation/delivery of pending interrupts at a clean
 * execution point (next main-loop iteration). Scheduled from a remap-while-asserted
 * so the CPU takes the routed interrupt cleanly, instead of synchronously re-entering
 * an ISR from inside the guest's own write to the interrupt-map register.
 */
static void esp32c3_intmatrix_reeval_bh(void *opaque)
{
    ESP32C3IntMatrixState *s = opaque;
    esp32c3_intmatrix_core_prio_changed(s, s->irq_thres);
}


/**
 * This function is called when the status (enabled/disabled) of a line has just been changed.
 * It will update the pending IRQ map.
 */
static void esp32c3_intmatrix_irq_status_changed(ESP32C3IntMatrixState* s, uint32_t line, int enabled)
{
    const bool accept = esp32c3_intmatrix_can_trigger(s);

    if (!enabled) {

        /* IRQ has just been disabled, if any interrupt is pending, clear it */
        CLEAR_BIT(s->irq_pending, line);

    } else if (esp32c3_get_output_line_level(s, line)) {

        /* IRQ has just been re-enabled, we have to check if any interrupt source is mapped to it, and
         * if that's the case, check if their level is high, as we would need to potentially trigger an
         * interrupt. */
        SET_BIT(s->irq_pending, line);

        if (accept) {
            /* If the CPU can accept interrupt, trigger an interrupt now */
            esp32c3_do_int(s, line);
        }
    }
}


/**
 * Callback invoked by the CPU as soon as interrupts are re-enabled
 */
static bool esp32c3_intmatrix_mie_enabled(void* opaque)
{
    ESP32C3IntMatrixState *s = ESP32C3_INTMATRIX(opaque);
    /* We need to check if any interrupt is pending and trigger it. We have such function already, triggered when
     * the core priority changes, let's reuse this function by giving the same core priority */
    esp32c3_intmatrix_core_prio_changed(s, s->irq_thres);
    return s->irq_pending != 0;
}


static uint64_t esp32c3_intmatrix_read(void* opaque, hwaddr addr, unsigned int size)
{
    ESP32C3IntMatrixState *s = ESP32C3_INTMATRIX(opaque);
    const uint32_t index = addr / sizeof(uint32_t);
    uint32_t r = 0;

    if (index < ESP32C3_INT_MATRIX_INPUTS) {
        r = s->irq_map[index];
    } else if (index >= ESP32C3_INTMATRIX_IO_PRIO_START && index < ESP32C3_INTMATRIX_IO_PRIO_END) {
        /* Interrupts start at 1, omit the first entry */
        const uint32_t line = index - ESP32C3_INTMATRIX_IO_PRIO_START + 1;
        r = s->irq_prio[line];
    } else if (index == ESP32C3_INTMATRIX_IO_THRESH_REG) {
        r = s->irq_thres;
    }  else if (index == ESP32C3_INTMATRIX_IO_ENABLE_REG) {
        r = s->irq_enabled;
    } else if (index == ESP32C3_INTMATRIX_IO_TYPE_REG) {
        r = 0;
    } else {
#if INTMATRIX_WARNING
        /* Other registers are not supported yet */
        warn_report("[INTMATRIX] Unsupported read to %08lx\n", addr);
#endif
    }

    return r;
}

static void esp32c3_intmatrix_write(void* opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESP32C3IntMatrixState *s = ESP32C3_INTMATRIX(opaque);

    const uint32_t index = addr / sizeof(uint32_t);

    if (index < ESP32C3_INT_MATRIX_INPUTS) {

        const int old_line = s->irq_map[index];
        s->irq_map[index] = (value & 0x1f);

        /* When a source is remapped to a *different* CPU line, the OLD line may
         * no longer have any asserted source. On real hardware a CPU line's
         * level is the OR of the levels of all sources mapped to it, so once
         * this source leaves, the old line de-asserts. Recompute the old line
         * and clear a now-stale pending bit. Without this, an ISR that disables
         * itself by remapping its source to a parked/disabled line — exactly
         * what the SPI bus-lock's bg_disable()/esp_intr_disable() does at ISR
         * entry — leaves the old line permanently pending, so it is re-delivered
         * the instant the handler returns and the ISR re-fires forever. */
        if (old_line != s->irq_map[index] && old_line != 0 &&
            esp32c3_get_output_line_level(s, old_line) == 0) {
            CLEAR_BIT(s->irq_pending, old_line);
        }

        /* Re-evaluate routing on a remap. The source's asserted level is unchanged,
         * but it now targets a (possibly different) CPU line. On real hardware the
         * level-triggered matrix immediately routes the current level to the new
         * line; without re-evaluating here, a source that is asserted *while* being
         * remapped onto an enabled line would never reach the CPU. This is exactly
         * what the SPI master does when it switches a completed/asserted transfer
         * from polled to interrupt-driven completion (remapping ETS_SPI2 onto an
         * active line while trans_done is still high) — without this it deadlocks. */
        {
            const int new_line = s->irq_map[index];
            if ((s->irq_levels & BIT(index)) && (s->irq_enabled & BIT(new_line))) {
                /* Mark the line pending and deliver it from a bottom-half on the
                 * next main-loop iteration. Delivering it synchronously here would
                 * pulse the CPU line from within the guest's interrupt-config write
                 * (e.g. esp_intr_enable's matrix-register store), re-entering an ISR
                 * at an unsafe point and leaving the CPU interrupt threshold stuck
                 * elevated. The BH re-evaluates once we're back at a clean point. */
                SET_BIT(s->irq_pending, new_line);
                qemu_bh_schedule(s->reeval_bh);
            }
        }
#if INTMATRIX_DEBUG
        info_report("\x1b[31m[INTMATRIX] Mapping interrupt %d to CPU line %d\x1b[0m\n", index, s->irq_map[index]);
#endif

    } else if (index >= ESP32C3_INTMATRIX_IO_PRIO_START && index < ESP32C3_INTMATRIX_IO_PRIO_END) {

        const uint8_t priority = value & 0xf;
        const uint32_t line = (index - ESP32C3_INTMATRIX_IO_PRIO_START) + 1;
        s->irq_prio[line] = priority;
#if INTMATRIX_DEBUG
        info_report("\x1b[31m[INTMATRIX] Priority of line %d set to %d\x1b[0m\n", line, priority);
#endif
        /* Check if the new priority interrupts the CPU */
        esp32c3_intmatrix_irq_prio_changed(s, line, priority);

    } else if (index == ESP32C3_INTMATRIX_IO_THRESH_REG) {

        const uint8_t priority = value & 0xf;

        /**
         * If the new priority is the same as the former one, nothing must be done.
         * Else, this could result in an infinite loop. Let's say we have an interrupt source
         * that is mapped to a CPU line, its threshold is 2, the CPU threshold is 3.
         * When the interrupt source is asserted, no interrupt is triggered, because the line's
         * priority is lower than the threshold but the its pending bit is set .
         * As soon as the threshold is lowered to 2 or 1, the interrupt will be triggered because
         * its pending bit is set.
         * NOTE THAT THE PENDING BIT IS STILL SET BECAUSE THE SOURCE IS STILL ASSERTED!
         * As such, if the CPU sets the threshold to the same value, the function
         * `esp32c3_intmatrix_core_prio_changed` called below would re-schedule the same interrupt.
         */
        if (priority != s->irq_thres) {
            s->irq_thres = priority;
#if INTMATRIX_DEBUG
            info_report("\x1b[31m[INTMATRIX] Setting CPU IRQ threshold to %d\x1b[0m", priority);
#endif
            esp32c3_intmatrix_core_prio_changed(s, priority);
        }

    } else if (index == ESP32C3_INTMATRIX_IO_ENABLE_REG) {
        /* Check if any bit has changed status */
        uint64_t prev = s->irq_enabled;
        s->irq_enabled = value;
        /* Check which interrupt/bit changed
         * Interrupts starts at 1, so we need to count up to ESP32C3_CPU_INT_COUNT */
        for (int i = 0; i <= ESP32C3_CPU_INT_COUNT; i++) {
            const int new_st = value & BIT(i);
            const int old_st = prev  & BIT(i);
            if (new_st != old_st) {
                esp32c3_intmatrix_irq_status_changed(s, i, new_st ? 1 : 0);
            }
        }
    } else if (index == ESP32C3_INTMATRIX_IO_TYPE_REG) {
        if (value != 0) {
#if INTMATRIX_WARNING
            warn_report("[INTMATRIX] Edge-triggered interrupts not supported\n");
#endif
        }
    } else {
#if INTMATRIX_WARNING
        /* Other registers are not supported yet */
        warn_report("[INTMATRIX] Unsupported write to %08lx (%08lx)\n", addr, value);
#endif
    }
}

static const MemoryRegionOps esp_intmatrix_ops = {
    .read =  esp32c3_intmatrix_read,
    .write = esp32c3_intmatrix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp32c3_intmatrix_reset_hold(Object *obj, ResetType type)
{
    ESP32C3IntMatrixState *s = ESP32C3_INTMATRIX(obj);
    RISCVCPU *cpu = &s->cpu->parent_obj;

    memset(s->irq_map, 0, sizeof(s->irq_map));
    memset(s->irq_prio, 0, sizeof(s->irq_prio));
    s->irq_thres = 0;
    s->irq_pending = 0;
    s->irq_levels = 0;
    s->irq_trigger = 0;
    s->irq_enabled = 0;
    for (int i = 0; i <= ESP32C3_CPU_INT_COUNT; i++) {
        qemu_irq_lower(s->out_irqs[i]);
    }

    /* Force the CPU to allow all interrupts */
    riscv_csr_write(&cpu->env, CSR_MIE, BIT(12) - 1);
}


static void esp32c3_intmatrix_realize(DeviceState *dev, Error **errp)
{
    ESP32C3IntMatrixState *s = ESP32C3_INTMATRIX(dev);
    EspRISCVCPU *cpu = s->cpu;
    EspRISCVCPUClass *cpu_klass = ESP_CPU_GET_CLASS(cpu);

    esp32c3_intmatrix_reset_hold(OBJECT(dev), RESET_TYPE_COLD);

    /* Bottom-half for deferred (safe-point) delivery of remap-while-asserted IRQs */
    s->reeval_bh = qemu_bh_new(esp32c3_intmatrix_reeval_bh, s);

    /* Register MIE callback */
    assert(cpu);
    cpu_klass->esp_cpu_register_mie_callback(cpu, esp32c3_intmatrix_mie_enabled, s);
}


static void esp32c3_intmatrix_init(Object *obj)
{
    ESP32C3IntMatrixState *s = ESP32C3_INTMATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_intmatrix_ops, s,
                          TYPE_ESP32C3_INTMATRIX, ESP32C3_INTMATRIX_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(DEVICE(s), esp32c3_intmatrix_irq_handler, ESP32C3_INT_MATRIX_INPUTS);
    qdev_init_gpio_out_named(DEVICE(s), s->out_irqs, ESP32C3_INT_MATRIX_OUTPUT_NAME, ESP32C3_CPU_INT_COUNT + 1);
}


static Property esp32c3_intmatrix_properties[] = {
    DEFINE_PROP_LINK("cpu", ESP32C3IntMatrixState, cpu, TYPE_ESP_RISCV_CPU, EspRISCVCPU*),
    DEFINE_PROP_END_OF_LIST(),
};


static void esp32c3_intmatrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c3_intmatrix_reset_hold;
    dc->realize = esp32c3_intmatrix_realize;
    device_class_set_props(dc, esp32c3_intmatrix_properties);
}


static const TypeInfo esp32c3_intmatrix_info = {
    .name = TYPE_ESP32C3_INTMATRIX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3IntMatrixState),
    .instance_init = esp32c3_intmatrix_init,
    .class_init = esp32c3_intmatrix_class_init
};


static void esp32c3_intmatrix_register_types(void)
{
    type_register_static(&esp32c3_intmatrix_info);
}


type_init(esp32c3_intmatrix_register_types)

/*
 * ESP32-C3 System Timer
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/timer/esp32c3_systimer.h"


#define TICKS_TO_NS(ticks) (((ticks) / ESP32C3_SYSTIMER_CNT_PER_US) * 1000)

#define SYSTIMER_DEBUG      0
#define SYSTIMER_WARNING    0

/**
 * @brief Update the value of a counter according the QEMU virtual timer.
 */
static void esp32c3_systimer_update_counter(ESP32C3SysTimerCounter *counter) {
    const int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const int64_t elapsed_ns = now - counter->base;
    const int64_t ticks = (elapsed_ns * (ESP32C3_SYSTIMER_CNT_CLK / 1000000)) / 1000;
    counter->value = (counter->value + ticks) & ESP32C3_SYSTIMER_52BIT_MASK;
    counter->base = now;
}


static void esp32c3_systimer_set_irqs(ESP32C3SysTimerState *s)
{
    for (int i = 0; i < ESP32C3_SYSTIMER_COMP_COUNT; i++) {
        ESP32C3SysTimerComp* comparator = &s->comparators[i];
        /* Optimize a bit by not calling the interrupt module if the state didn't change */
        const int new_level = comparator->raw_st && comparator->int_enabled;
        if (new_level != comparator->cur_irq_level) {
            comparator->cur_irq_level = new_level;
            qemu_set_irq(comparator->irq, new_level);
        }
    }
}


static void esp32c3_systimer_notify(ESP32C3SysTimerComp* comparator)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
#if SYSTIMER_DEBUG
    static int64_t previous = 0;
    info_report("Elapsed since last interrupt %ld ns\n", now - previous);
    previous = now;
#endif

    /* Update the counter linked to the comparator */
    const uint32_t counter_idx = comparator->counter;
    ESP32C3SysTimerState *s = comparator->systimer;
    ESP32C3SysTimerCounter* counter = &s->counter[counter_idx];

    if (counter->enabled) {
        esp32c3_systimer_update_counter(counter);
    }

    /* Set the IRQ line if anything changed */
    comparator->cur_irq_level = 1;
    qemu_irq_raise(comparator->irq);

    /* Check if we have to reload the comparator's timer */
    if (counter->enabled && comparator->period_mode) {
        /**
         * If the timer is periodic, we have to compensate the delay that may have been taken
         * by QEMU framework. Indeed, this timer may have been triggered too late (tenth of us)
         * As such, compensate here by triggering the next timer earlier.
         */
        const int64_t diff_ns = now - comparator->expire_time;
        const int64_t target_ns = now + TICKS_TO_NS(comparator->period);
        int64_t adjusted_target_ns = target_ns;

        if (diff_ns < target_ns) {
            adjusted_target_ns = target_ns - diff_ns;
        }

        comparator->expire_time = adjusted_target_ns;
        timer_mod_ns(&comparator->qtimer, adjusted_target_ns);
    }
}


/**
 * @brief Function called when the configuration of the given comparator changed.
 * It will reprogram the QEMU Timer according to the new parameters. The comparator
 * must be enabled when this function is called.
 */
static void esp32c3_systimer_comparator_reprogram(ESP32C3SysTimerComp* comparator)
{
    assert(comparator->enabled);

    const uint32_t counter_idx = comparator->counter;
    ESP32C3SysTimerState *s = comparator->systimer;
    ESP32C3SysTimerCounter* counter = &s->counter[counter_idx];

    /* Update the counter to get the latest value */
    esp32c3_systimer_update_counter(counter);

    /* If the counter we have to compare it to is not enabled, do not program any timer */
    if (!counter->enabled) {
        /* "Disable" the timer */
        timer_del(&comparator->qtimer);
        return;
    }

    /**
     * According to the TRM, if the comparator (alarm) is smaller than the counter value and the
     * difference is bigger or equal to (2^51) - 1, the alarm is not triggered right now and the counter
     * will have to overflow first before reached the comparator value.
     */
    const int64_t count_val = counter->value;
    const int64_t alarm = comparator->value;
    const bool overflow_valid = alarm < count_val && count_val - alarm >= ((1ULL << 51) - 1);

    if (!comparator->period_mode && !overflow_valid && alarm <= count_val) {
        timer_del(&comparator->qtimer);
        esp32c3_systimer_notify(comparator);
        return;
    }

    /* Calculate in how many ns the counter would reach the comparator */
    uint64_t diff = 0;

    if (comparator->period_mode) {
        diff = comparator->period;
    } else if (overflow_valid) {
        diff = ((ESP32C3_SYSTIMER_52BIT_MASK + 1) - count_val) + alarm;
    } else {
        diff = alarm - count_val;
    }

    /* Calculate the number of ns the counter will take to reach the absolute count */
    const int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const int64_t target_ns = now + TICKS_TO_NS(diff);
    comparator->expire_time = target_ns;
    timer_mod_ns(&comparator->qtimer, target_ns);
}


static uint32_t esp32c3_systimer_comparator_get_conf(ESP32C3SysTimerComp *comp)
{
    uint32_t reg = comp->period & R_SYSTIMER_TARGET0_CONF_PERIOD_MASK;
    reg |= (comp->period_mode << R_SYSTIMER_TARGET0_CONF_PERIOD_MODE_SHIFT) & R_SYSTIMER_TARGET0_CONF_PERIOD_MODE_MASK;
    reg |= (comp->counter << R_SYSTIMER_TARGET0_CONF_TIMER_UNIT_SEL_SHIFT) & R_SYSTIMER_TARGET0_CONF_TIMER_UNIT_SEL_MASK;
    return reg;
}


static uint64_t esp32c3_systimer_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3SysTimerState *s = ESP32C3_SYSTIMER(opaque);
    uint64_t r = 0;

    switch (addr) {
        case A_SYSTIMER_CONF:
            r = s->conf;
            break;
        case A_SYSTIMER_UNIT0_OP:
        case A_SYSTIMER_UNIT1_OP:
            /* Value is always synchronized and valid */
            r = R_SYSTIMER_UNIT0_OP_VALUE_VALID_MASK;
            break;

        /* Registers linked to UNIT0 registers */
        case A_SYSTIMER_UNIT0_VALUE_HI:
            r = SYSTIMER_GET_REG(s->counter[0].flushed, 32, R_SYSTIMER_UNIT0_VALUE_HI_TIMER_HI_MASK);
            break;
        case A_SYSTIMER_UNIT0_VALUE_LO:
            r = SYSTIMER_GET_REG(s->counter[0].flushed, 0, R_SYSTIMER_UNIT0_VALUE_LO_TIMER_LO_MASK);
            break;
        case A_SYSTIMER_UNIT0_LOAD_HI:
            r = SYSTIMER_GET_REG(s->counter[0].toload, 32, R_SYSTIMER_UNIT0_LOAD_HI_HI_MASK);
            break;
        case A_SYSTIMER_UNIT0_LOAD_LO:
            r = SYSTIMER_GET_REG(s->counter[0].toload, 0, R_SYSTIMER_UNIT0_LOAD_LO_LO_MASK);
            break;

        /* Same for UNIT1 */
        case A_SYSTIMER_UNIT1_VALUE_HI:
            r = SYSTIMER_GET_REG(s->counter[1].flushed, 32, R_SYSTIMER_UNIT1_VALUE_HI_TIMER_HI_MASK);
            break;
        case A_SYSTIMER_UNIT1_VALUE_LO:
            r = SYSTIMER_GET_REG(s->counter[1].flushed, 0, R_SYSTIMER_UNIT1_VALUE_LO_TIMER_LO_MASK);
            break;
        case A_SYSTIMER_UNIT1_LOAD_HI:
            r = SYSTIMER_GET_REG(s->counter[1].toload, 32, R_SYSTIMER_UNIT1_LOAD_HI_HI_MASK);
            break;
        case A_SYSTIMER_UNIT1_LOAD_LO:
            r = SYSTIMER_GET_REG(s->counter[1].toload, 0, R_SYSTIMER_UNIT1_LOAD_LO_LO_MASK);
            break;

        /* Read back the value of the comparators */
        case A_SYSTIMER_TARGET0_HI:
            r = SYSTIMER_GET_REG(s->comparators[0].value_toload, 32, R_SYSTIMER_TARGET0_HI_HI_MASK);
            break;
        case A_SYSTIMER_TARGET0_LO:
            r = SYSTIMER_GET_REG(s->comparators[0].value_toload, 0, R_SYSTIMER_TARGET0_LO_LO_MASK);
            break;
        case A_SYSTIMER_TARGET0_CONF:
            r = esp32c3_systimer_comparator_get_conf(&s->comparators[0]);
            break;

        /* Registers linked to Comparator 1 */
        case A_SYSTIMER_TARGET1_HI:
            r = SYSTIMER_GET_REG(s->comparators[1].value_toload, 32, R_SYSTIMER_TARGET1_HI_HI_MASK);
            break;
        case A_SYSTIMER_TARGET1_LO:
            r = SYSTIMER_GET_REG(s->comparators[1].value_toload, 0, R_SYSTIMER_TARGET1_LO_LO_MASK);
            break;
        case A_SYSTIMER_TARGET1_CONF:
            r = esp32c3_systimer_comparator_get_conf(&s->comparators[1]);
            break;

        /* Registers linked to Comparator 2 */
        case A_SYSTIMER_TARGET2_HI:
            r = SYSTIMER_GET_REG(s->comparators[2].value_toload, 32, R_SYSTIMER_TARGET2_HI_HI_MASK);
            break;
        case A_SYSTIMER_TARGET2_LO:
            r = SYSTIMER_GET_REG(s->comparators[2].value_toload, 0, R_SYSTIMER_TARGET2_LO_LO_MASK);
            break;
        case A_SYSTIMER_TARGET2_CONF:
            r = esp32c3_systimer_comparator_get_conf(&s->comparators[2]);
            break;

        /* Interrupt related */
        case A_SYSTIMER_INT_RAW:
            r = s->comparators[0].raw_st << R_SYSTIMER_INT_RAW_TARGET0_SHIFT
              | s->comparators[1].raw_st << R_SYSTIMER_INT_RAW_TARGET1_SHIFT
              | s->comparators[2].raw_st << R_SYSTIMER_INT_RAW_TARGET2_SHIFT;
            break;

        case A_SYSTIMER_INT_ENA:
            r = s->comparators[0].int_enabled << R_SYSTIMER_INT_ENA_TARGET0_SHIFT
              | s->comparators[1].int_enabled << R_SYSTIMER_INT_ENA_TARGET1_SHIFT
              | s->comparators[2].int_enabled << R_SYSTIMER_INT_ENA_TARGET2_SHIFT;
            break;

        case A_SYSTIMER_INT_ST:
            r = (s->comparators[0].raw_st & s->comparators[0].int_enabled) << R_SYSTIMER_INT_ST_TARGET0_SHIFT
              | (s->comparators[1].raw_st & s->comparators[1].int_enabled) << R_SYSTIMER_INT_ST_TARGET1_SHIFT
              | (s->comparators[2].raw_st & s->comparators[2].int_enabled) << R_SYSTIMER_INT_ST_TARGET2_SHIFT;
            break;

        case A_SYSTIMER_INT_CLR:
            /* This register is read-only */
            break;

        default:
#if SYSTIMER_WARNING
            warn_report("[SYSTIMER] Unsupported read to %08lx\n", addr);
#endif
            break;
    }

#if SYSTIMER_DEBUG
    info_report("[SYSTIMER] Reading from %08lx (%08lx)\n", addr, r);
#endif
    return r;
}

/**
 * @brief Function called when a guest program requests a flush of the internal
 * counter inside a mirror register: `flushed`.
 */
static void esp32c3_systimer_flush_counter(ESP32C3SysTimerCounter *counter)
{
    /* If the counter is enabled, get its latest value */
    if (counter->enabled) {
        esp32c3_systimer_update_counter(counter);
    }
    /* In any case, flush the value to send to the guest program */
    counter->flushed = counter->value;
}

/**
 * @brief Function called when the guest programs wants to replace the value
 * of the given counter
 */
static void esp32c3_systimer_load_counter(ESP32C3SysTimerState *s, uint32_t index)
{
    ESP32C3SysTimerCounter* counter = &s->counter[index];

    /* If the counter is enabled, update the internal value to the latest count.
     * Even though the value will be replaced, this will also update the base time. */
    if (counter->enabled) {
        esp32c3_systimer_update_counter(counter);
    }
    counter->value = counter->toload;

    /* If one of the comparator is enabled and depends on the current timer, reprogram it */
    for (int i = 0; i < ESP32C3_SYSTIMER_COMP_COUNT; i++) {
        if (s->comparators[i].enabled && s->comparators[i].counter == index) {
            esp32c3_systimer_comparator_reprogram(&s->comparators[i]);
        }
    }
}


static void esp32c3_systimer_comparator_conf(ESP32C3SysTimerComp* comparator, uint64_t value)
{
    uint32_t old_counter = comparator->counter;
    comparator->period_mode = (value & R_SYSTIMER_TARGET0_CONF_PERIOD_MODE_MASK) ? 1 : 0;
    comparator->counter = (value & R_SYSTIMER_TARGET0_CONF_TIMER_UNIT_SEL_MASK) ? 1 : 0;
    comparator->period_toload = value & R_SYSTIMER_TARGET0_CONF_PERIOD_MASK;

    /* If the comparator is enabled while the counter to compare it to changed, reload the timer! */
    if (comparator->enabled && old_counter != comparator->counter) {
        esp32c3_systimer_comparator_reprogram(comparator);
    }
}


static void esp32c3_systimer_comparator_reload(ESP32C3SysTimerComp* comparator)
{
    comparator->period = comparator->period_toload;
    comparator->value = comparator->value_toload;

    if (comparator->enabled) {
        /* The period changed, reprogram the QEMU timer */
        esp32c3_systimer_comparator_reprogram(comparator);
    }
}


static void esp32c3_systimer_conf_set(ESP32C3SysTimerState *s, uint64_t value)
{
    bool state[ESP32C3_SYSTIMER_COMP_COUNT] = { 0 };
    bool state_counter[ESP32C3_SYSTIMER_COUNTER_COUNT] = { 0 };

    for (int i = 0; i < ESP32C3_SYSTIMER_COUNTER_COUNT; i++) {
        state_counter[i] = s->counter[i].enabled;
    }
    s->counter[0].enabled = (value & R_SYSTIMER_CONF_TIMER_UNIT0_WORK_EN_MASK) ? 1 : 0;
    s->counter[1].enabled = (value & R_SYSTIMER_CONF_TIMER_UNIT1_WORK_EN_MASK) ? 1 : 0;
    s->counter[0].enabled_on_stall = (value & R_SYSTIMER_CONF_TIMER_UNIT0_CORE0_STALL_EN_MASK) ? 1 : 0;
    s->counter[1].enabled_on_stall = (value & R_SYSTIMER_CONF_TIMER_UNIT1_CORE0_STALL_EN_MASK) ? 1 : 0;

    /* If the state of the counter changed while one of the comparator depends on it, reload it */
    for (int i = 0; i < ESP32C3_SYSTIMER_COMP_COUNT; i++) {
        const int count_idx = s->comparators[i].counter;

        if (s->comparators[i].enabled && state_counter[count_idx] != s->counter[count_idx].enabled) {
            esp32c3_systimer_comparator_reprogram(&s->comparators[i]);
        }
    }


    /**
     * Do the same thing for the comparators: assign the new state while checking if a change occurred,
     * if that's the case, reprogram it.
     */
    for (int i = 0; i < ESP32C3_SYSTIMER_COMP_COUNT; i++) {
        state[i] = s->comparators[i].enabled;
    }

    s->comparators[0].enabled = (value & R_SYSTIMER_CONF_TARGET0_WORK_EN_MASK) ? 1 : 0;
    s->comparators[1].enabled = (value & R_SYSTIMER_CONF_TARGET1_WORK_EN_MASK) ? 1 : 0;
    s->comparators[2].enabled = (value & R_SYSTIMER_CONF_TARGET2_WORK_EN_MASK) ? 1 : 0;

    for (int i = 0; i < ESP32C3_SYSTIMER_COMP_COUNT; i++) {
        if (state[i] == s->comparators[i].enabled) {
            continue;
        }

        /* If the comparator has just been enabled, (re)program it.
         * If it was enabled and it is now disabled, disable the QEMU timer. */
        if (s->comparators[i].enabled) {
            esp32c3_systimer_comparator_reprogram(&s->comparators[i]);
        } else {
            timer_del(&s->comparators[i].qtimer);
        }
    }

    /* Ignore R_SYSTIMER_CONF_CLK_EN */
    s->conf = (uint32_t) value;
}


static void esp32c3_systimer_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned int size)
{
    ESP32C3SysTimerState *s = ESP32C3_SYSTIMER(opaque);
    (void) s;

    switch(addr) {
        case A_SYSTIMER_CONF:
            esp32c3_systimer_conf_set(s, value);
            break;
        case A_SYSTIMER_UNIT0_OP:
        case A_SYSTIMER_UNIT1_OP:
            /* Check if an update was requested on one of the timers */
            if (value & R_SYSTIMER_UNIT0_OP_UPDATE_MASK) {
                /* Address is either 4 (unit0) or 8 (unit 1), get index by dividing by 8 */
                esp32c3_systimer_flush_counter(&s->counter[addr / 8]);
            }
            break;

        /* Registers linked to UNIT0 registers */
        case A_SYSTIMER_UNIT0_LOAD_HI:
            systimer_set_reg(&s->counter[0].toload, value, R_SYSTIMER_UNIT0_LOAD_HI_HI_MASK, 32);
            break;
        case A_SYSTIMER_UNIT0_LOAD_LO:
            systimer_set_reg(&s->counter[0].toload, value, R_SYSTIMER_UNIT0_LOAD_LO_LO_MASK, 0);
            break;
        case A_SYSTIMER_UNIT0_LOAD:
            esp32c3_systimer_load_counter(s, 0);
            break;

        /* Registers linked to UNIT1 registers */
        case A_SYSTIMER_UNIT1_LOAD_HI:
            systimer_set_reg(&s->counter[1].toload, value, R_SYSTIMER_UNIT1_LOAD_HI_HI_MASK, 32);
            break;
        case A_SYSTIMER_UNIT1_LOAD_LO:
            systimer_set_reg(&s->counter[1].toload, value, R_SYSTIMER_UNIT1_LOAD_LO_LO_MASK, 0);
            break;
        case A_SYSTIMER_UNIT1_LOAD:
            esp32c3_systimer_load_counter(s, 1);
            break;

        /* Registers linked to Comparator 0 */
        case A_SYSTIMER_TARGET0_HI:
            systimer_set_reg(&s->comparators[0].value_toload, value, R_SYSTIMER_TARGET0_HI_HI_MASK, 32);
            break;
        case A_SYSTIMER_TARGET0_LO:
            systimer_set_reg(&s->comparators[0].value_toload, value, R_SYSTIMER_TARGET0_LO_LO_MASK, 0);
            break;
        case A_SYSTIMER_TARGET0_CONF:
            esp32c3_systimer_comparator_conf(&s->comparators[0], value);
            break;
        case A_SYSTIMER_COMP0_LOAD:
            if (value & R_SYSTIMER_COMP0_LOAD_LOAD_MASK) {
                esp32c3_systimer_comparator_reload(&s->comparators[0]);
            }
            break;

        /* Registers linked to Comparator 1 */
        case A_SYSTIMER_TARGET1_HI:
            systimer_set_reg(&s->comparators[1].value_toload, value, R_SYSTIMER_TARGET1_HI_HI_MASK, 32);
            break;
        case A_SYSTIMER_TARGET1_LO:
            systimer_set_reg(&s->comparators[1].value_toload, value, R_SYSTIMER_TARGET1_LO_LO_MASK, 0);
            break;
        case A_SYSTIMER_TARGET1_CONF:
            esp32c3_systimer_comparator_conf(&s->comparators[1], value);
            break;
        case A_SYSTIMER_COMP1_LOAD:
            if (value & R_SYSTIMER_COMP1_LOAD_LOAD_MASK) {
                esp32c3_systimer_comparator_reload(&s->comparators[1]);
            }
            break;

        /* Registers linked to Comparator 2 */
        case A_SYSTIMER_TARGET2_HI:
            systimer_set_reg(&s->comparators[2].value_toload, value, R_SYSTIMER_TARGET2_HI_HI_MASK, 32);
            break;
        case A_SYSTIMER_TARGET2_LO:
            systimer_set_reg(&s->comparators[2].value_toload, value, R_SYSTIMER_TARGET2_LO_LO_MASK, 0);
            break;
        case A_SYSTIMER_TARGET2_CONF:
            esp32c3_systimer_comparator_conf(&s->comparators[2], value);
            break;
        case A_SYSTIMER_COMP2_LOAD:
            if (value & R_SYSTIMER_COMP2_LOAD_LOAD_MASK) {
                esp32c3_systimer_comparator_reload(&s->comparators[2]);
            }
            break;

        /* Interrupt related */
        case A_SYSTIMER_INT_RAW:
        case A_SYSTIMER_INT_ST:
#if SYSTIMER_WARNING
            warn_report("[SYSTIMER] Unsupported write to INT RAW/ST registers\n");
#endif
            break;

        case A_SYSTIMER_INT_ENA:
            s->comparators[0].int_enabled = FIELD_EX32(value, SYSTIMER_INT_ENA, TARGET0);
            s->comparators[1].int_enabled = FIELD_EX32(value, SYSTIMER_INT_ENA, TARGET1);
            s->comparators[2].int_enabled = FIELD_EX32(value, SYSTIMER_INT_ENA, TARGET2);
            esp32c3_systimer_set_irqs(s);
            break;

        case A_SYSTIMER_INT_CLR:
            /* The formula (RAW & !CLR) results in 0 if both CLR and RAW were 1, else it keeps
             * RAW unchanged. */
            s->comparators[0].raw_st &= ! FIELD_EX32(value, SYSTIMER_INT_CLR, TARGET0);
            s->comparators[1].raw_st &= ! FIELD_EX32(value, SYSTIMER_INT_CLR, TARGET1);
            s->comparators[2].raw_st &= ! FIELD_EX32(value, SYSTIMER_INT_CLR, TARGET2);
            esp32c3_systimer_set_irqs(s);
            break;

        default:
#if SYSTIMER_WARNING
            warn_report("[SYSTIMER] Unsupported write to registers %08lx (%08lx)\n", addr, value);
#endif
            break;
    }

#if SYSTIMER_DEBUG
    info_report("[SYSTIMER] Writing to %08lx = %08lx\n", addr, value);
#endif
}


static const MemoryRegionOps esp32c3_systimer_ops = {
    .read =  esp32c3_systimer_read,
    .write = esp32c3_systimer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_systimer_cb(void* opaque)
{
    ESP32C3SysTimerComp* comparator = (ESP32C3SysTimerComp*) opaque;
    esp32c3_systimer_notify(comparator);
}


static void esp32c3_systimer_reset(DeviceState* ts)
{
    ESP32C3SysTimerState *s = ESP32C3_SYSTIMER(ts);
    s->conf = 0;
    for (int i = 0; i < ESP32C3_SYSTIMER_COMP_COUNT; i++) {
        ESP32C3SysTimerComp* comp = &s->comparators[i];
        QEMUTimer qtimer = comp->qtimer;
        qemu_irq irq = comp->irq;

        /* Disable all the timers/irq first */
        timer_del(&comp->qtimer);
        qemu_irq_lower(comp->irq);

        /* Reset the data of the comparator */
        memset(comp, 0, sizeof(ESP32C3SysTimerComp));

        /* Restore the former fields that were already initialized */
        comp->qtimer = qtimer;
        comp->irq = irq;
        comp->systimer = s;
    }
}


static void esp32c3_systimer_realize(DeviceState *dev, Error **errp)
{
}


static void esp32c3_systimer_init(Object *obj)
{
    ESP32C3SysTimerState *s = ESP32C3_SYSTIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_systimer_ops, s,
                          TYPE_ESP32C3_SYSTIMER, ESP32C3_SYSTIMER_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    for (uint64_t i = 0; i < ESP32C3_SYSTIMER_COMP_COUNT; i++) {
        s->comparators[i].systimer = s;
        sysbus_init_irq(sbd, &s->comparators[i].irq);
        timer_init_ns(&s->comparators[i].qtimer, QEMU_CLOCK_VIRTUAL, esp32c3_systimer_cb, &s->comparators[i]);
    }
}


static void esp32c3_systimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_systimer_reset;
    dc->realize = esp32c3_systimer_realize;
}


static const TypeInfo esp32c3_systimer_info = {
    .name = TYPE_ESP32C3_SYSTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3SysTimerState),
    .instance_init = esp32c3_systimer_init,
    .class_init = esp32c3_systimer_class_init
};


static void esp32c3_systimer_register_types(void)
{
    type_register_static(&esp32c3_systimer_info);
}


type_init(esp32c3_systimer_register_types)

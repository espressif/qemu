/*
 * ESP32 FRC (legacy) timer
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
#include "qapi/visitor.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "hw/timer/esp32_frc_timer.h"
#include "trace.h"

static uint64_t esp32_frc_timer_get_count(Esp32FrcTimerState *s, uint64_t ns_now)
{
    if (!s->enable) {
        return s->count_base;
    }
    uint64_t ns_from_base = ns_now - s->ns_base;
    uint64_t ticks_from_base = muldiv64(ns_from_base, s->apb_freq, NANOSECONDS_PER_SECOND * s->prescaler);
    uint64_t count = (ticks_from_base + s->count_base) & s->count_mask;
    return count;
}

static uint64_t esp32_frc_timer_count_to_ns(Esp32FrcTimerState *s, uint64_t count)
{
    return muldiv64(count, NANOSECONDS_PER_SECOND * s->prescaler, s->apb_freq);
}

static void esp32_frc_timer_update_alarm(Esp32FrcTimerState *s, uint64_t ticks_alarm, uint32_t ticks_now, uint64_t ns_now)
{
    if (ticks_alarm <= ticks_now) {
        ticks_alarm += (1ULL << 32);
    }
    uint64_t ticks_to_alarm = ticks_alarm - ticks_now;
    uint64_t ns_to_alarm = esp32_frc_timer_count_to_ns(s, ticks_to_alarm);
    trace_esp32_frc_timer_update_alarm(ns_now, ticks_now, ticks_alarm, ns_to_alarm);
    timer_mod_anticipate_ns(&s->alarm_timer, ns_now + ns_to_alarm);
}

static void esp32_frc_timer_cb(void *opaque)
{
    Esp32FrcTimerState *s = ESP32_FRC_TIMER(opaque);
    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t count_now = esp32_frc_timer_get_count(s, ns_now);
    trace_esp32_frc_timer_cb(ns_now, count_now);

    if (s->level_int) {
        s->level_int_status = true;
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_pulse(s->irq);
    }

    if (s->autoload) {
        esp32_frc_timer_update_alarm(s, s->alarm_reg, count_now, ns_now);
    }
}

static void esp32_frc_timer_update_config(Esp32FrcTimerState *s,
                                          bool enable, bool level_int, bool autoload, int prescaler)
{
    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t count_now = esp32_frc_timer_get_count(s, ns_now);
    trace_esp32_frc_timer_update_config(ns_now, count_now, enable, level_int, autoload, prescaler);

    s->count_base = count_now;
    s->ns_base = ns_now;

    if (!enable) {
        timer_del(&s->alarm_timer);
    } else {
        esp32_frc_timer_update_alarm(s, s->alarm_reg, count_now, ns_now);
    }

    s->enable = enable;
    s->level_int = level_int;
    s->autoload = autoload;
    s->prescaler = prescaler;
}


static uint64_t esp32_frc_timer_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32FrcTimerState *s = ESP32_FRC_TIMER(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_FRC_TIMER_ALARM:
        r = s->alarm_reg;
        break;
    case A_FRC_TIMER_LOAD:
        r = s->load_reg;
        break;
    case A_FRC_TIMER_CTRL:
        r = FIELD_DP32(r, FRC_TIMER_CTRL, LEVEL_INT, s->level_int);
        r = FIELD_DP32(r, FRC_TIMER_CTRL, PRESCALER, (s->prescaler == 1) ? 0 : (s->prescaler == 16) ? 2 : 4);
        r = FIELD_DP32(r, FRC_TIMER_CTRL, AUTOLOAD, s->autoload);
        r = FIELD_DP32(r, FRC_TIMER_CTRL, ENABLE, s->enable);
        r = FIELD_DP32(r, FRC_TIMER_CTRL, STATUS, s->level_int_status);
        break;
    case A_FRC_TIMER_COUNT: {
        uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        r = (uint32_t) esp32_frc_timer_get_count(s, ns_now);
        break;
    }
    case A_FRC_TIMER_INT_CLR:
    default:
        break;
    }
    trace_esp32_frc_timer_read(addr, r);
    return r;
}

static void esp32_frc_timer_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32FrcTimerState *s = ESP32_FRC_TIMER(opaque);
    trace_esp32_frc_timer_write(addr, value);
    switch (addr) {
    case A_FRC_TIMER_INT_CLR:
        if (value & 1) {
            if (s->level_int_status) {
                qemu_irq_lower(s->irq);
            }
            s->level_int_status = false;
        }
        break;
    case A_FRC_TIMER_LOAD:
        s->load_reg = value;
        s->ns_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->count_base = value;
        break;
    case A_FRC_TIMER_CTRL: {
        bool level_int = FIELD_EX32(value, FRC_TIMER_CTRL, LEVEL_INT);
        int prescaler = FIELD_EX32(value, FRC_TIMER_CTRL, PRESCALER);
        bool autoload = FIELD_EX32(value, FRC_TIMER_CTRL, AUTOLOAD);
        bool enable = FIELD_EX32(value, FRC_TIMER_CTRL, ENABLE);
        if (prescaler == 2) {
            prescaler = 16;
        } else if (prescaler == 4) {
            prescaler = 256;
        } else {
            prescaler = 1;
        }
        esp32_frc_timer_update_config(s, enable, level_int, autoload, prescaler);
        break;
    }
    case A_FRC_TIMER_ALARM: {
        uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint32_t count_now = esp32_frc_timer_get_count(s, ns_now);
        s->alarm_reg = value;
        esp32_frc_timer_update_alarm(s, s->alarm_reg, count_now, ns_now);
        break;
    }
    }
}

static void esp32_frc_timer_set_apb_freq(Object *obj, Visitor *v,
                                  const char *name, void *opaque,
                                  Error **errp)
{
    Esp32FrcTimerState *s = ESP32_FRC_TIMER(opaque);
    visit_type_uint32(v, name, &s->apb_freq, errp);

}

static const MemoryRegionOps esp32_frc_timer_ops = {
    .read =  esp32_frc_timer_read,
    .write = esp32_frc_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_frc_timer_reset(DeviceState *dev)
{
    Esp32FrcTimerState *s = ESP32_FRC_TIMER(dev);

    s->prescaler = 1;
}

static void esp32_frc_timer_realize(DeviceState *dev, Error **errp)
{
}

static void esp32_frc_timer_init(Object *obj)
{
    Esp32FrcTimerState *s = ESP32_FRC_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_frc_timer_ops, s,
                          TYPE_ESP32_FRC_TIMER, A_FRC_TIMER_ALARM + 4);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    object_property_add(obj, "apb_freq", "uint32",
                        NULL,
                        esp32_frc_timer_set_apb_freq,
                        NULL,
                        obj);


    timer_init_ns(&s->alarm_timer, QEMU_CLOCK_VIRTUAL, esp32_frc_timer_cb, s);

    s->apb_freq = 80000000;
    s->count_mask = UINT32_MAX;
    s->has_alarm = true;
}

static Property esp32_frc_timer_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_frc_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_frc_timer_reset;
    dc->realize = esp32_frc_timer_realize;
    device_class_set_props(dc, esp32_frc_timer_properties);
}

static const TypeInfo esp32_frc_timer_info = {
    .name = TYPE_ESP32_FRC_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32FrcTimerState),
    .instance_init = esp32_frc_timer_init,
    .class_init = esp32_frc_timer_class_init
};

static void esp32_frc_timer_register_types(void)
{
    type_register_static(&esp32_frc_timer_info);
}

type_init(esp32_frc_timer_register_types)

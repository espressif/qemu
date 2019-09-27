/*
 * ESP32 "Timer Group" peripheral
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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/timer/esp32_timg.h"


#define TIMG_REGFILE_SIZE 0x100

static uint64_t esp32_timg_timer_get_count(Esp32TimgTimerState *s, uint64_t ns_now);
static uint64_t esp32_timg_timer_count_to_ns(Esp32TimgTimerState *s, uint64_t count);
static void esp32_timg_timer_update_config(Esp32TimgTimerState *ts);
static void esp32_timg_timer_update_alarm(Esp32TimgTimerState *ts, uint64_t ns_now);
static void esp32_timg_timer_reload(Esp32TimgTimerState *ts, uint64_t ns_now);
static void esp32_timg_do_calibration(Esp32TimgState* s);
static void esp32_timg_int_update(Esp32TimgState *s);
static bool esp32_timg_wdt_protected(Esp32TimgWdtState *ws);
static void esp32_timg_wdt_update_config(Esp32TimgWdtState *ws);
static void esp32_timg_wdt_feed(Esp32TimgWdtState *ws);
static void esp32_timg_wdt_arm(Esp32TimgWdtState *ws, uint64_t ns_now);


#define TIMG_DEBUG_LOG(...) // qemu_log(__VA_ARGS__)

static inline void set_low_word(uint64_t *dst, uint32_t word)
{
    *dst = (*dst & ~UINT32_MAX) | word;
}

static inline void set_high_word(uint64_t *dst, uint32_t word)
{
    *dst = (*dst & UINT32_MAX) | (((uint64_t)word) << 32);
}

static inline qemu_irq get_level_irq(Esp32TimgState* s, Esp32TimgInterruptType it)
{
    return s->irqs[it];
}

static inline qemu_irq get_edge_irq(Esp32TimgState* s, Esp32TimgInterruptType it)
{
    return s->irqs[TIMG_INT_MAX + it];
}


static uint64_t esp32_timg_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32TimgState *s = ESP32_TIMG(opaque);
    Esp32TimgTimerState *ts = NULL;
    if (addr <= A_TIMG_T0LOAD) {
        ts = &s->t0;
    } else if (addr <= A_TIMG_T1LOAD) {
        ts = &s->t1;
    } else if (addr >= A_TIMG_LACTCONFIG && addr < A_TIMG_LACTLOAD) {
        ts = &s->lact;
    }
    uint64_t r = 0;
    switch (addr) {
    case A_TIMG_T0CONFIG:
    case A_TIMG_T1CONFIG:
    case A_TIMG_LACTCONFIG:
        r = ts->config_reg;
        break;
    case A_TIMG_T0LO:
    case A_TIMG_T1LO:
    case A_TIMG_LACTLO:
        r = ts->last_val & UINT32_MAX;
        break;
    case A_TIMG_T0HI:
    case A_TIMG_T1HI:
    case A_TIMG_LACTHI:
        r = ts->last_val >> 32;
        break;
    case A_TIMG_T0LOADLO:
    case A_TIMG_T1LOADLO:
    case A_TIMG_LACTLOADLO:
        r = ts->load_val & UINT32_MAX;
        break;
    case A_TIMG_T0LOADHI:
    case A_TIMG_T1LOADHI:
    case A_TIMG_LACTLOADHI:
        r = ts->load_val >> 32;
        break;
    case A_TIMG_T0ALARMLO:
    case A_TIMG_T1ALARMLO:
    case A_TIMG_LACTALARMLO:
        r = ts->alarm_val & UINT32_MAX;
        break;
    case A_TIMG_T0ALARMHI:
    case A_TIMG_T1ALARMHI:
    case A_TIMG_LACTALARMHI:
        r = ts->alarm_val >> 32;
        break;

    case A_TIMG_WDTCONFIG0:
        r = s->wdt.config0_reg;
        break;
    case A_TIMG_WDTCONFIG1:
        r = FIELD_DP32(r, TIMG_WDTCONFIG1, PRESCALE, s->wdt.prescale);
        break;
    case A_TIMG_WDTCONFIG2:
    case A_TIMG_WDTCONFIG3:
    case A_TIMG_WDTCONFIG4:
    case A_TIMG_WDTCONFIG5: {
        int stage = (addr - A_TIMG_WDTCONFIG2) / 4;
        r = s->wdt.timeout[stage];
        break;
    }
    case A_TIMG_WDTPROTECT:
        r = s->wdt.protect_reg;
        break;

    case A_TIMG_RTCCALICFG:
        r = FIELD_DP32(r, TIMG_RTCCALICFG, START, s->rtc_cal_start);
        r = FIELD_DP32(r, TIMG_RTCCALICFG, MAX, s->rtc_cal_max);
        r = FIELD_DP32(r, TIMG_RTCCALICFG, RDY, s->rtc_cal_ready);
        r = FIELD_DP32(r, TIMG_RTCCALICFG, CLK_SEL, s->rtc_cal_clk_sel);
        break;
    case A_TIMG_RTCCALICFG1:
        r = FIELD_DP32(0, TIMG_RTCCALICFG1, VALUE, s->rtc_cal_value);
        break;

    case A_TIMG_INT_ENA:
        r = s->int_ena;
        break;
    case A_TIMG_INT_RAW:
        r = s->int_raw;
        break;
    case A_TIMG_INT_ST:
        r = s->int_ena & s->int_raw;
        break;
    }
    return r;
}

static void esp32_timg_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32TimgState *s = ESP32_TIMG(opaque);
    Esp32TimgTimerState *ts = NULL;
    if (addr <= A_TIMG_T0LOAD) {
        ts = &s->t0;
    } else if (addr <= A_TIMG_T1LOAD) {
        ts = &s->t1;
    } else if (addr >= A_TIMG_LACTCONFIG && addr <= A_TIMG_LACTLOAD) {
        ts = &s->lact;
    }

    switch (addr) {
    case A_TIMG_T0CONFIG:
    case A_TIMG_T1CONFIG:
    case A_TIMG_LACTCONFIG:
        ts->config_reg = value;
        esp32_timg_timer_update_config(ts);
        break;
    case A_TIMG_T0UPDATE:
    case A_TIMG_T1UPDATE:
    case A_TIMG_LACTUPDATE: {
        uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        ts->last_val = esp32_timg_timer_get_count(ts, ns_now);
        break;
    }
    case A_TIMG_T0LOADLO:
    case A_TIMG_T1LOADLO:
    case A_TIMG_LACTLOADLO:
        set_low_word(&ts->load_val, value);
        break;
    case A_TIMG_T0LOADHI:
    case A_TIMG_T1LOADHI:
    case A_TIMG_LACTLOADHI:
        set_high_word(&ts->load_val, value);
        break;
    case A_TIMG_T0LOAD:
    case A_TIMG_T1LOAD:
    case A_TIMG_LACTLOAD: {
        uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        esp32_timg_timer_reload(ts, ns_now);
        break;
    }
    case A_TIMG_T0ALARMLO:
    case A_TIMG_T1ALARMLO:
    case A_TIMG_LACTALARMLO:
        set_low_word(&ts->alarm_val, value);
        break;
    case A_TIMG_T0ALARMHI:
    case A_TIMG_T1ALARMHI:
    case A_TIMG_LACTALARMHI:
        set_high_word(&ts->alarm_val, value);
        break;

    case A_TIMG_WDTCONFIG0:
        if (!esp32_timg_wdt_protected(&s->wdt)) {
            s->wdt.config0_reg = value;
            esp32_timg_wdt_update_config(&s->wdt);
        } else {
            TIMG_DEBUG_LOG("failed to write TIMG_WDTCONFIG0, write protected (0x%08x)\n", s->wdt.protect_reg);
        }
        break;
    case A_TIMG_WDTCONFIG1:
        if (!esp32_timg_wdt_protected(&s->wdt)) {
            s->wdt.config1_reg = value;
            esp32_timg_wdt_update_config(&s->wdt);
        }
        break;
    case A_TIMG_WDTCONFIG2:
    case A_TIMG_WDTCONFIG3:
    case A_TIMG_WDTCONFIG4:
    case A_TIMG_WDTCONFIG5: {
        if (!esp32_timg_wdt_protected(&s->wdt)) {
            int stage = (addr - A_TIMG_WDTCONFIG2) / 4;
            s->wdt.timeout[stage] = value;
        }
        break;
    }
    case A_TIMG_WDTFEED:
        if (!esp32_timg_wdt_protected(&s->wdt)) {
            esp32_timg_wdt_feed(&s->wdt);
        }
        break;
    case A_TIMG_WDTPROTECT:
        s->wdt.protect_reg = value;
        break;


    case A_TIMG_RTCCALICFG:
        s->rtc_cal_start = FIELD_EX32(value, TIMG_RTCCALICFG, START);
        s->rtc_cal_ready = FIELD_EX32(value, TIMG_RTCCALICFG, RDY);
        s->rtc_cal_clk_sel = FIELD_EX32(value, TIMG_RTCCALICFG, CLK_SEL);
        s->rtc_cal_max = FIELD_EX32(value, TIMG_RTCCALICFG, MAX);
        esp32_timg_do_calibration(s);
        break;

    case A_TIMG_INT_ENA:
        s->int_ena = value;
        esp32_timg_int_update(s);
        break;
    case A_TIMG_INT_CLR:
        s->int_raw &= ~value;
        esp32_timg_int_update(s);
        break;
    }
}

static const MemoryRegionOps esp32_timg_ops = {
    .read =  esp32_timg_read,
    .write = esp32_timg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_timg_timer_reset(Esp32TimgTimerState* ts)
{
    timer_del(&ts->alarm_timer);
    ts->config_reg = R_TIMG_T0CONFIG_INCREASE_MASK
        | R_TIMG_T0CONFIG_AUTORELOAD_MASK
        | (1 << R_TIMG_T0CONFIG_DIVIDER_SHIFT);
    ts->alarm_val = 0;
    ts->load_val = 0;
    ts->count_base = 0;
    ts->ns_base = 0;
    esp32_timg_timer_update_config(ts);
}

static void esp32_timg_wdt_reset(Esp32TimgWdtState* ws)
{
    timer_del(&ws->stage_timer);

    ws->config0_reg = 0x0004c000;
    ws->config1_reg = 0x00010000;
    ws->timeout[0] = 0x018cba80;
    ws->timeout[1] = 0x07ffffff;
    ws->timeout[2] = 0x000fffff;
    ws->timeout[3] = 0x000fffff;
    ws->protect_reg = ESP32_TIMG_WDT_PROTECT_WORD;

    if (ws->parent->wdt_en_at_reset) {
        /* On reset, stage0 is configured as system reset, however this is done by
         * hardware state, not in config0 register. Emulate this by temporarily setting STG0 field here.
         */
        ws->config0_reg |= (WDT_MODE_SYSRESET << R_TIMG_WDTCONFIG0_STG0_SHIFT);
    }
    esp32_timg_wdt_update_config(ws);
    if (ws->parent->wdt_en_at_reset) {
        ws->config0_reg &= (~R_TIMG_WDTCONFIG0_STG0_MASK);
    }
}

static void esp32_timg_reset(DeviceState *dev)
{
    Esp32TimgState *s = ESP32_TIMG(dev);
    s->rtc_cal_max = 1;
    s->rtc_cal_clk_sel = ESP32_TIMG_CAL_8MD256;
    s->rtc_cal_ready = 0;
    s->rtc_cal_start = 1;
    esp32_timg_do_calibration(s);

    esp32_timg_timer_reset(&s->t0);
    esp32_timg_timer_reset(&s->t1);
    esp32_timg_timer_reset(&s->lact);
    esp32_timg_wdt_reset(&s->wdt);
}

static void esp32_timg_set_apb_freq(Object *obj, Visitor *v,
                                  const char *name, void *opaque,
                                  Error **errp)
{
    Esp32TimgState *s = ESP32_TIMG(opaque);
    visit_type_uint32(v, name, &s->apb_freq_hz, errp);
    TIMG_DEBUG_LOG("%s: TG%d apb_freq_hz=%d\n", __func__, s->id, s->apb_freq_hz);
}

static void esp32_timg_do_calibration(Esp32TimgState* s)
{
    uint32_t cal_clk_freq;
    if (s->rtc_cal_clk_sel == ESP32_TIMG_CAL_RTC_MUX) {
        cal_clk_freq = s->rtc_slow_freq_hz;
    } else if (s->rtc_cal_clk_sel == ESP32_TIMG_CAL_32K_XTAL) {
        cal_clk_freq = 32768;
    } else {
        cal_clk_freq = 8000000 / 256;
    }

    s->rtc_cal_value = muldiv64(s->xtal_freq_hz, s->rtc_cal_max, cal_clk_freq);
    s->rtc_cal_ready = true;
}

static void esp32_timg_timer_cb(void *opaque)
{
    Esp32TimgTimerState *ts = (Esp32TimgTimerState*) opaque;
    Esp32TimgState *s = ts->parent;
    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    TIMG_DEBUG_LOG("%s: TG%d ns=0x%llx\n", __func__, s->id, ns_now);
    uint32_t int_mask = 1 << (ts->int_type);

    if (ts->level_int_en) {
        s->int_raw |= int_mask;
        if (s->int_ena & int_mask) {
            qemu_irq_raise(get_level_irq(s, ts->int_type));
        }
    }

    if (ts->edge_int_en) {
        if (s->int_ena & int_mask) {
            qemu_irq_pulse(get_edge_irq(s, ts->int_type));
        }
    }

    ts->alarm = false;

    if (ts->autoreload) {
        esp32_timg_timer_reload(ts, ns_now);
    } else {
        /* ignore overflow modulo 64 bits */
        timer_del(&ts->alarm_timer);
    }
}

static void esp32_timg_int_update_inttype(Esp32TimgState *s, uint32_t int_st, Esp32TimgInterruptType it)
{
    if (int_st & (1 << it)) {
        qemu_irq_raise(get_level_irq(s, it));
    } else {
        qemu_irq_lower(get_level_irq(s, it));
    }
}

static void esp32_timg_int_update(Esp32TimgState *s)
{
    uint32_t int_st = s->int_ena & s->int_raw;
    esp32_timg_int_update_inttype(s, int_st, TIMG_T0_INT);
    esp32_timg_int_update_inttype(s, int_st, TIMG_T1_INT);
    esp32_timg_int_update_inttype(s, int_st, TIMG_WDT_INT);
    esp32_timg_int_update_inttype(s, int_st, TIMG_LACT_INT);
}

static int esp32_timg_timer_direction(Esp32TimgTimerState *s)
{
    if (!s->en) {
        return 0;
    }
    if (s->inc) {
        return 1;
    } else {
        return -1;
    }
}

static uint64_t esp32_timg_timer_get_count(Esp32TimgTimerState *s, uint64_t ns_now)
{
    if (!s->en) {
        return s->count_base;
    }
    uint64_t ns_from_base = ns_now - s->ns_base;
    uint64_t ticks_from_base = muldiv64(ns_from_base, s->parent->apb_freq_hz / 1000000, 1000 * s->divider);
    uint64_t count = esp32_timg_timer_direction(s) * ticks_from_base + s->count_base;
    return count;
}

static uint64_t esp32_timg_timer_count_to_ns(Esp32TimgTimerState *s, uint64_t count)
{
    return muldiv64(count, 1000 * s->divider, s->parent->apb_freq_hz / 1000000);
}

static uint32_t esp32_timg_timer_div_from_reg(uint32_t reg_val)
{
    if (reg_val == 0) {
        return 65536;
    }
    if (reg_val == 1 || reg_val == 2) {
        return 2;
    }
    return reg_val;
}

static void esp32_timg_timer_update_config(Esp32TimgTimerState *ts)
{
    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    ts->count_base = esp32_timg_timer_get_count(ts, ns_now);
    ts->ns_base = ns_now;

    ts->en = FIELD_EX32(ts->config_reg, TIMG_T0CONFIG, EN);
    ts->inc = FIELD_EX32(ts->config_reg, TIMG_T0CONFIG, INCREASE);
    ts->autoreload = FIELD_EX32(ts->config_reg, TIMG_T0CONFIG, AUTORELOAD);
    ts->divider = esp32_timg_timer_div_from_reg(FIELD_EX32(ts->config_reg, TIMG_T0CONFIG, DIVIDER));
    ts->edge_int_en = FIELD_EX32(ts->config_reg, TIMG_T0CONFIG, EDGE_INT);
    ts->level_int_en = FIELD_EX32(ts->config_reg, TIMG_T0CONFIG, LEVEL_INT);
    ts->alarm = FIELD_EX32(ts->config_reg, TIMG_T0CONFIG, ALARM);

    TIMG_DEBUG_LOG("%s: TG%d base=0x%llx ns=0x%llx en=%d inc=%d autoreload=%d div=%d li=%d ei=%d alarm=%d\n", __func__, ts->parent->id,
             ts->count_base, ts->ns_base, ts->en, ts->inc, ts->autoreload, ts->divider,
             ts->level_int_en, ts->edge_int_en, ts->alarm);

    esp32_timg_timer_update_alarm(ts, ns_now);
}

static void esp32_timg_timer_reload(Esp32TimgTimerState *ts, uint64_t ns_now)
{
    timer_del(&ts->alarm_timer);

    ts->ns_base = ns_now;
    ts->count_base = ts->load_val;

    TIMG_DEBUG_LOG("%s: TG%d base=0x%llx ns=0x%llx\n", __func__, ts->parent->id,
             ts->count_base, ts->ns_base);

    esp32_timg_timer_update_alarm(ts, ns_now);
}

static void esp32_timg_timer_update_alarm(Esp32TimgTimerState *ts, uint64_t ns_now)
{
    if (!ts->en || !ts->alarm) {
        timer_del(&ts->alarm_timer);
        return;
    }

    int64_t count_to_alarm = ((int64_t) ts->alarm_val - (int64_t) ts->count_base)
                                * esp32_timg_timer_direction(ts);
    if (count_to_alarm <= 0) {
        /* ignore overflow modulo 64 bits */
        timer_del(&ts->alarm_timer);
        return;
    }

    uint64_t ns_to_alarm = esp32_timg_timer_count_to_ns(ts, count_to_alarm);

    TIMG_DEBUG_LOG("%s: TG%d count_to_alarm=0x%llx ns_to_alarm=0x%llx\n", __func__, ts->parent->id,
                 count_to_alarm, ns_to_alarm);

    timer_mod_anticipate_ns(&ts->alarm_timer, ns_now + ns_to_alarm);
}

static bool esp32_timg_wdt_protected(Esp32TimgWdtState *ws)
{
    return ws->protect_reg != ESP32_TIMG_WDT_PROTECT_WORD;
}

static uint64_t esp32_timg_wdt_get_count(Esp32TimgWdtState *ws, uint64_t ns_now)
{
    if (!ws->en) {
        return ws->count_base;
    }
    uint64_t ns_from_base = ns_now - ws->ns_base;
    uint64_t ticks_from_base = muldiv64(ns_from_base, ws->parent->apb_freq_hz / 1000000, 1000 * MAX(ws->prescale, 1));
    uint64_t count = ticks_from_base + ws->count_base;
    return count;
}

static void esp32_timg_wdt_update_config(Esp32TimgWdtState *ws)
{
    Esp32TimgState *s = ws->parent;

    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    ws->count_base = esp32_timg_wdt_get_count(ws, ns_now);
    ws->ns_base = ns_now;

    bool old_en = ws->en;
    ws->en = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, EN);
    ws->mode[0] = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, STG0);
    ws->mode[1] = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, STG1);
    ws->mode[2] = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, STG2);
    ws->mode[3] = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, STG3);
    ws->edge_int_en = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, EDGE_INT);
    ws->level_int_en = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, LEVEL_INT);
    ws->flashboot_en = FIELD_EX32(ws->config0_reg, TIMG_WDTCONFIG0, FLASHBOOT_MODE_EN);

    ws->prescale = FIELD_EX32(ws->config1_reg, TIMG_WDTCONFIG1, PRESCALE);

    if (ws->en && !old_en) {
        ws->cur_stage = 0;
        ws->count_base = 0;
    } else if (!ws->en && old_en) {
        qemu_irq_lower(get_level_irq(s, TIMG_WDT_INT));
    }

    TIMG_DEBUG_LOG("%s: TG%d config 0x%08x prescale=0x%08x en=%d fb_en=%d level_int_en=%d\n", __func__, ws->parent->id,
                   ws->config0_reg, ws->prescale, ws->en, ws->flashboot_en, ws->level_int_en);
    esp32_timg_wdt_arm(ws, ns_now);
}

static void esp32_timg_wdt_feed(Esp32TimgWdtState *ws)
{
    TIMG_DEBUG_LOG("%s TG%d\n", __func__, ws->parent->id);
    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    ws->cur_stage = 0;
    ws->ns_base = ns_now;
    ws->count_base = 0;
    esp32_timg_wdt_arm(ws, ns_now);
}

static void esp32_timg_wdt_arm(Esp32TimgWdtState *ws, uint64_t ns_now)
{
    timer_del(&ws->stage_timer);

    if (ws->parent->wdt_disable || !(ws->en || (ws->flashboot_en && ws->parent->flash_boot_mode))) {
        return;
    }

    uint32_t stage_timeout = ws->timeout[ws->cur_stage];
    uint32_t cur_count = esp32_timg_wdt_get_count(ws, ns_now);
    uint32_t count_to_timeout = stage_timeout - cur_count;
    uint64_t ns_to_timeout = muldiv64(count_to_timeout, 1000 * ws->prescale, ws->parent->apb_freq_hz / 1000000);
    TIMG_DEBUG_LOG("%s: TG%d ns=0x%08llx stage %d count=0x%08x count_to_timeout=0x%08x ns_to_timeout=0x%08llx\n",
                   __func__, ws->parent->id, ns_now, ws->cur_stage, cur_count, count_to_timeout, ns_to_timeout);
    timer_mod_anticipate_ns(&ws->stage_timer, ns_now + ns_to_timeout);
}

static void esp32_timg_wdt_cb(void *opaque)
{
    Esp32TimgWdtState *ws = (Esp32TimgWdtState*) opaque;
    Esp32TimgState *s = ws->parent;
    Esp32TimgWdtStageMode mode = ws->mode[ws->cur_stage];
    TIMG_DEBUG_LOG("%s: TG%d stage %d timeout mode %d\n", __func__, s->id, ws->cur_stage, mode);
    if (mode == WDT_MODE_INT) {
        uint32_t mask = 1 << TIMG_WDT_INT;
        if (ws->level_int_en) {
            s->int_raw |= mask;
            if (true) { /* should be checking s->int_ena & mask, but ESP32 seems to raise interrupt regardless? */
                qemu_irq_raise(get_level_irq(s, TIMG_WDT_INT));
            }
        }
        if (ws->edge_int_en) {
            if (s->int_ena & mask) {
                qemu_irq_pulse(get_edge_irq(s, TIMG_WDT_INT));
            }
        }
    } else if (mode == WDT_MODE_CPURESET) {
        qemu_irq_pulse(s->wdt_cpu_reset_req);
    } else if (mode == WDT_MODE_SYSRESET) {
        qemu_irq_pulse(s->wdt_sys_reset_req);
    }

    int next_stage = (ws->cur_stage + 1) % ESP32_TIMG_WDT_STAGE_COUNT;
    uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    ws->count_base = 0;
    ws->cur_stage = next_stage;
    ws->ns_base = ns_now;
    esp32_timg_wdt_arm(ws, ns_now);
}

static void esp32_timg_realize(DeviceState *dev, Error **errp)
{
}

static void esp32_timg_timer_init(Esp32TimgState *s, Esp32TimgTimerState *ts, Esp32TimgInterruptType int_type) {
    ts->parent = s;
    timer_init_ns(&ts->alarm_timer, QEMU_CLOCK_VIRTUAL, esp32_timg_timer_cb, ts);
    ts->int_type = int_type;
}

static void esp32_timg_init(Object *obj)
{
    Esp32TimgState *s = ESP32_TIMG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_timg_ops, s,
                          TYPE_ESP32_TIMG, TIMG_REGFILE_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_out_named(DEVICE(sbd), s->irqs, SYSBUS_DEVICE_GPIO_IRQ, 2*TIMG_INT_MAX);

    object_property_add(obj, "apb_freq", "uint32",
                        NULL,
                        esp32_timg_set_apb_freq,
                        NULL,
                        obj);

    s->rtc_slow_freq_hz = 150000;
    s->xtal_freq_hz = 40000000;
    s->apb_freq_hz = 40000000;

    esp32_timg_timer_init(s, &s->t0, TIMG_T0_INT);
    esp32_timg_timer_init(s, &s->t1, TIMG_T1_INT);
    esp32_timg_timer_init(s, &s->lact, TIMG_LACT_INT);

    s->wdt.parent = s;
    timer_init_ns(&s->wdt.stage_timer, QEMU_CLOCK_VIRTUAL, esp32_timg_wdt_cb, &s->wdt);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->wdt_cpu_reset_req, ESP32_TIMG_WDT_CPU_RESET_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->wdt_sys_reset_req, ESP32_TIMG_WDT_SYS_RESET_GPIO, 1);
}

static Property esp32_timg_properties[] = {
    DEFINE_PROP_BOOL("wdt_disable", Esp32TimgState, wdt_disable, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_timg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_timg_reset;
    dc->realize = esp32_timg_realize;
    device_class_set_props(dc, esp32_timg_properties);
}

static const TypeInfo esp32_timg_info = {
    .name = TYPE_ESP32_TIMG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32TimgState),
    .instance_init = esp32_timg_init,
    .class_init = esp32_timg_class_init
};

static void esp32_timg_register_types(void)
{
    type_register_static(&esp32_timg_info);
}

type_init(esp32_timg_register_types)

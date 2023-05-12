/*
 * ESP32-C3 "Timer Group" peripheral
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
#include "hw/timer/esp32c3_timg.h"

#define TIMG_DEBUG      0
#define TIMG_WARNING    0

#define FIELD_CHANGED(value1, value2, reg, field) \
    ((value1) & R_ ## reg ## _ ## field ## _MASK) != ((value2) & R_ ## reg ## _ ## field ## _MASK)

/**
 * Helper to load a 32-bit low value and a 22-bit high value into a 64-bit value
 */
static inline uint64_t load_low(uint64_t reg, uint32_t low)
{
    return (reg & (0xffffffff00000000)) | (low & UINT32_MAX);
}

static inline uint64_t load_high(uint64_t reg, uint32_t high)
{
    return (reg & UINT32_MAX) | ((uint64_t) (high & 0x3fffff) << 32);
}


/**
 * @brief Update the value of a counter according the QEMU virtual timer.
 */
static int64_t esp32c3_virtual_counter_update(ESP32C3VirtualCounter *counter)
{
    const int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const int64_t elapsed_ns = now - counter->base;
    const int64_t ticks = (elapsed_ns * (counter->frequency / 1000)) / 1000000;
    counter->value += ticks;
    counter->base = now;
    return counter->value;
}

static inline QEMUTimer* esp32c3_virtual_counter_get_timer(ESP32C3VirtualCounter *counter)
{
    return &counter->timer;
}

static void esp32c3_virtual_counter_alarm_in_ticks(ESP32C3VirtualCounter *counter, int64_t ticks)
{
    int64_t delay_ns = (ticks * (1000000000UL / counter->frequency));
    const int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* This function will reschedule the clock if it was already scheduled */
    counter->base = now;
    counter->value = 0;
    timer_mod_ns(&counter->timer, now + delay_ns);
}

/**
 * Update the time base of the timer without updating the counter value.
 * This shall be used when the counter has just been re-enabled, and the elapsed time since it was disabled
 * must not be taken into account.
 */
static void esp32c3_virtual_counter_reenabled(ESP32C3VirtualCounter *counter)
{
    counter->base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void esp32c3_virtual_counter_reset(ESP32C3VirtualCounter* counter)
{
    timer_del(&counter->timer);
    counter->base = 0;
    counter->value = 0;
}


/**
 * @brief Function called when an update of the RTC Calibration register is requested
 *        Perform the required calibration simulation here and update the register.
 */
static void esp32c3_timg_rtc_cali_update(ESP32C3TimgState *s, uint32_t value)
{
    const uint32_t osc_freq_arr[] = {
        [ESP32C3_TIMG_CALI_RC_SLOW_CLK]     = ESP32C3_RC_SLOW_FREQ,
        [ESP32C3_TIMG_CALI_RC_FAST_DIV_CLK] = ESP32C3_RC_FAST_DIV_FREQ,
        [ESP32C3_TIMG_CALI_XTAL32K_CLK]     = ESP32C3_XTAL32K_FREQ
    };
    /* Copy the new value to the register but keep RDY bit to 0 (read-only) */
    value &= ~(R_TIMG_RTCCALICFG_RDY_MASK);

    /* Check if a start (one-time or periodic) was triggered */
    if (value & (R_TIMG_RTCCALICFG_START_MASK | R_TIMG_RTCCALICFG_START_CYCLING_MASK)) {

        /* Get the clock that is being calibrated */
        const uint32_t clk = FIELD_EX32(value, TIMG_RTCCALICFG, CLK_SEL);
        const uint32_t freq = osc_freq_arr[clk];

        /* And the counter that should be reached by this clock */
        const uint32_t max_count = FIELD_EX32(value, TIMG_RTCCALICFG, MAX);

        /* Calculate how many clock cycle it would require to the XTAL_CLK to reach this count */
        const uint32_t xtal_count = (ESP32C3_XTAL_CLK * max_count) / freq;

        /* Save this count in the RTC Calibration register 1 */
        s->rtc.rtc_cali_cfg_result = xtal_count << R_TIMG_RTCCALICFG1_VALUE_SHIFT;

        value |= R_TIMG_RTCCALICFG_RDY_MASK;

        /* Clear the timeout register */
        s->rtc.rtc_cali_cfg_timeout &= ~(R_TIMG_RTCCALICFG2_TIMEOUT_MASK);
    }

    s->rtc.rtc_cali_cfg = value;
}

/**
 * @brief Function called when an update on the timeout register occur.
 */
static void esp32c3_timg_rtc_cali_check_timeout(ESP32C3TimgState *s, uint32_t value)
{
    /* Let's simplify the process of timeout generation, if the timeout reset count is smaller
     * than the max cali count divided by a constant, generate a timeout  */
    const uint32_t count = FIELD_EX32(s->rtc.rtc_cali_cfg, TIMG_RTCCALICFG, MAX);
    const uint32_t rst_cnt = FIELD_EX32(value, TIMG_RTCCALICFG2, TIMEOUT_RST_CNT);

    s->rtc.rtc_cali_cfg_timeout = value & ~(R_TIMG_RTCCALICFG2_TIMEOUT_MASK);

    if (rst_cnt == 0 || (rst_cnt < count / 10))
    {
        s->rtc.rtc_cali_cfg_timeout |= R_TIMG_RTCCALICFG2_TIMEOUT_MASK;
    }
}

/**
 * Functions related to Watchdog
 */

static inline uint64_t esp32c3_wdt_ext_clk_frequency(ESP32C3WdtState* wdt)
{
    return FIELD_EX32(wdt->config0, TIMG_WDTCONFIG0, USE_XTAL) ? ESP32C3_XTAL_CLK : ESP32C3_APB_CLK;
}

static inline bool esp32c3_wdt_is_writable(ESP32C3WdtState* wdt)
{
    return wdt->wkey == ESP32C3_WDT_DEFAULT_WKEY;
}

static inline bool esp32c3_wdt_enabled(ESP32C3WdtState* wdt)
{
    return FIELD_EX32(wdt->config0, TIMG_T0CONFIG, EN) ? 1 : 0;
}


static void esp32c3_wdt_cb(void* opaque)
{
    ESP32C3WdtState* wdt = (ESP32C3WdtState*) opaque;
    const int cur_stage = wdt->current_stage;
    ESP32C3WdtStageConf conf = wdt->stage_conf[cur_stage];

    /* Check which action must be taken for the current stage */
    if (conf == ESP32C3_WDT_INTERRUPT) {
        wdt->raw_st = 1;
        if (wdt->int_enabled) {
            qemu_irq_raise(wdt->interrupt_irq);
        }
    } else if (conf == ESP32C3_WDT_RESET_CPU || conf == ESP32C3_WDT_RESET_SYS) {
        qemu_irq_raise(wdt->reset_irq);
        /* Do not schedule anything if we have to reset the machine */
        return;
    }

    const int new_stage = (cur_stage + 1)  % ESP32C3_WDT_STAGE_COUNT;
    wdt->current_stage = new_stage;

    if (conf == ESP32C3_WDT_OFF) {
        /* If the current stage is disabled, the counter shall not be reset to 0!
         * Get the number of ticks elapsed to calculate the remaining ticks before the next stage alarm.
         * A simpler option would be to reuse wdt->stage[cur_stage], but if the application modified this
         * register after scheduling an alarm, the result would be undefined. */
        const int64_t elapsed = esp32c3_virtual_counter_update(&wdt->counter);
        esp32c3_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[new_stage] - elapsed);
    } else {
        esp32c3_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[new_stage]);
    }
}

static void esp32c3_wdt_update_prescaler(ESP32C3WdtState* wdt, uint32_t value)
{
    if (FIELD_EX32(value, TIMG_WDTCONFIG1, DIVCNT_RST) || value == 0) {
        /* Avoid any divide-by-0 error in the code below */
        wdt->prescaler = 1;
    } else {
        wdt->prescaler = FIELD_EX32(value, TIMG_WDTCONFIG1, CLK_PRESCALE);
    }

    /* Recalculate the frequency out of the new prescaler and current clock */
    wdt->counter.frequency = esp32c3_wdt_ext_clk_frequency(wdt) / wdt->prescaler;

    /* In theory we should reschedule the timer if it is currently running.
     * In practice, let's say that this behavior is invalid  and do not reschedule it. */
}

static void esp32c3_wdt_update_stage(ESP32C3WdtState* wdt, int index, uint32_t value, bool verify)
{
    wdt->stage[index] = value;

    /* If the updated stage is the current one and the watchdog is enabled, reprogram the timer */
    if (esp32c3_wdt_enabled(wdt) && wdt->current_stage == index && verify) {
        /* Update the counter of the running timer, so that we can adjust the alarm */
        int64_t counter_value = esp32c3_virtual_counter_update(&wdt->counter);
        int64_t diff = (int64_t) value - counter_value;
        if (diff <= 0) {
            /* On the real hardware, the WDT is simply disabled if the new comparator value for the current
             * stage is smaller than the current value. It will be restarted (not resumed) when fed.
             * Just like the real hardware, keep the "enable" bit to 1, moreover it is required for feeding.
             */
            timer_del(&wdt->counter.timer);
        } else {
            /* The new alarm is set to happen in `diff` ticks, reschedule the alarm */
            esp32c3_virtual_counter_alarm_in_ticks(&wdt->counter, diff);
        }
    }
}

static void esp32c3_wdt_feed(ESP32C3WdtState* wdt)
{
    if (esp32c3_wdt_enabled(wdt)) {
        wdt->current_stage = 0;
        esp32c3_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[0]);
    }
}

static void esp32c3_wdt_update_config(ESP32C3WdtState* wdt, uint32_t value)
{
    /* If the WDT is protected return */
    if (!esp32c3_wdt_is_writable(wdt)) {
        return;
    }

    const uint32_t former_conf = wdt->config0;
    /* Clean the reserved bits */
    wdt->config0 = value & ~(R_TIMG_WDTCONFIG0_CONF_UPDATE_EN_MASK | 0x7ff);

    const bool enabled = FIELD_EX32(value, TIMG_WDTCONFIG0, EN) ? true : false;
    const bool enabled_changed = FIELD_EX32(former_conf, TIMG_WDTCONFIG0, EN) != enabled;

    if (FIELD_EX32(value, TIMG_WDTCONFIG0, CONF_UPDATE_EN)) {
        /* If the prescaler value or the source clock changed update the timer */
        if ((FIELD_EX32(wdt->prescaler_mirror, TIMG_WDTCONFIG1, CLK_PRESCALE) != wdt->prescaler) ||
            (FIELD_EX32(former_conf, TIMG_WDTCONFIG0, USE_XTAL) != FIELD_EX32(value, TIMG_WDTCONFIG0, USE_XTAL)))
        {
            esp32c3_wdt_update_prescaler(wdt, wdt->prescaler_mirror);
        }

        /* Update the stage configuration mirror */
        wdt->stage_conf[0] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG0);
        wdt->stage_conf[1] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG1);
        wdt->stage_conf[2] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG2);
        wdt->stage_conf[3] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG3);

        /* Update the stage values */
        for (int i = 0; i < ESP32C3_WDT_STAGE_COUNT; i++) {
            /* Only reprogram the timer if the enable flag didn't change */
            esp32c3_wdt_update_stage(wdt, i, wdt->stage_mirror[i], enabled && !enabled_changed);
        }
    }

    /* Check if the enabled bit changed */
    if (enabled_changed) {
        if (enabled) {
            wdt->config0 |= R_TIMG_WDTCONFIG0_EN_MASK;
            /* Timer has just been (re-)enabled, schedule the timer */
            esp32c3_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[0]);
        } else {
            wdt->config0 &= ~R_TIMG_WDTCONFIG0_EN_MASK;
            /* Disable the timer! */
            timer_del(&wdt->counter.timer);
        }
    }
}

/**
 * Functions related to T0 timer registers
 */
static void esp32c3_t0_update_counter(ESP32C3T0State* t)
{
    int64_t previous = t->counter.value;
    int64_t current = esp32c3_virtual_counter_update(&t->counter);
    int64_t delta = current - previous;
    const bool increase = FIELD_EX32(t->config, TIMG_T0CONFIG, INCREASE) ? true : false;
    if (increase) {
        t->value_rel = (t->value_rel + delta) & ESP32C3_TIMG_T0_MAX_VALUE;
    } else {
        t->value_rel = (t->value_rel - delta) & ESP32C3_TIMG_T0_MAX_VALUE;
    }
}


static void esp32c3_t0_cb(void* opaque)
{
    ESP32C3T0State* t = (ESP32C3T0State*) opaque;

    /* Update the value of the counter and disable the alarm timer */
    esp32c3_virtual_counter_reset(&t->counter);
    esp32c3_virtual_counter_reenabled(&t->counter);

    /* In practice, the counter is bigger than the requested value, this is due to the fact
     * that there is a cost of emulation and the (Linux) kernel timer may also be busy
     * doing something else before scheduling the VM. Adjust the counter to the alarm value. */
    t->value_rel = t->alarm;

    /* If the counter is set to auto-reload, set its new value */
    if (FIELD_EX32(t->config, TIMG_T0CONFIG, AUTORELOAD)) {
        t->value_rel = t->value_toload;
    }

    /* Alarm was triggered, clear alarm bit, set the IRQ if interrupts enabled */
    t->config &= ~R_TIMG_T0CONFIG_ALARM_EN_MASK;
    t->raw_st = true;
    if (t->int_enabled) {
        qemu_irq_raise(t->interrupt_irq);
    }
}


static void esp32c3_t0_counter_flush(ESP32C3T0State* t)
{
    if (FIELD_EX32(t->config, TIMG_T0CONFIG, EN)) {
        esp32c3_t0_update_counter(t);
    }

    t->value_flushed = t->value_rel;
}


static void esp32c3_t0_alarm_update(ESP32C3T0State* t)
{

    if (FIELD_EX32(t->config, TIMG_T0CONFIG, EN) &&
        FIELD_EX32(t->config, TIMG_T0CONFIG, ALARM_EN)) {

        const bool increase = FIELD_EX32(t->config, TIMG_T0CONFIG, INCREASE) ? true : false;
        const bool decrease = !increase;
        const uint64_t alarm = t->alarm;

        /* Update the current value of the relative counter */
        esp32c3_t0_update_counter(t);

        /* No matter if we increase or decrease the counter the time difference is the same */
        const uint64_t value = t->value_rel;
        uint64_t diff = llabs(alarm - value);
        const uint64_t limit = ESP32C3_TIMG_T0_LIMIT;

        /* Declare all the possible scenarios as explained in the TRM */
        const bool scenario1 = alarm >  value && diff >  limit;
        const bool scenario2 = alarm >  value && diff <= limit;
        const bool scenario3 = value >= alarm && diff <  limit;
        const bool scenario4 = value >= alarm && diff >= limit;
        const bool scenario5 = alarm <  value && diff >  limit;
        const bool scenario6 = alarm <  value && diff <= limit;
        const bool scenario7 = value <= alarm && diff <  limit;
        const bool scenario8 = value <= alarm && diff >= limit;

        if ((increase && (scenario1 || scenario3)) || (decrease && (scenario5 || scenario7))) {
            /* The alarm was programmed too late, trigger an interrupt manually */
            esp32c3_t0_cb(t);
        } else if ((increase && scenario2) || (decrease && scenario6)) {
            /* The alarm is in range and in the future, program its trigger */
            esp32c3_virtual_counter_alarm_in_ticks(&t->counter, diff);
        } else {
            assert(scenario4 || scenario8);
            /* The alarm is in range, in the future, but requires the timer to overflow/underflow */
            const uint64_t high = MAX(alarm, value);
            const uint64_t low  = MIN(alarm, value);
            /* Calculate the new (tick) difference between them */
            diff = (ESP32C3_TIMG_T0_MAX_VALUE + 1 - high) + low;
            esp32c3_virtual_counter_alarm_in_ticks(&t->counter, diff);
        }

    }
}


static void esp32c3_t0_counter_load(ESP32C3T0State* t)
{
    /* Update the counter so that the (time) base is up to date */
    esp32c3_t0_update_counter(t);

    /* Set the new counter */
    t->value_rel = t->value_toload;

    /* Reprogram the alarm if necessary */
    esp32c3_t0_alarm_update(t);
}

static void esp32c3_t0_config_update(ESP32C3T0State* t0, uint32_t value)
{
    const uint32_t former_conf = t0->config;
    /* Assign the new configuration while removing the write-only bits */
    t0->config = value & ~(R_TIMG_T0CONFIG_DIVCNT_RST_MASK);

    /* If the counter was enabled until now, update its value */
    if (former_conf & R_TIMG_T0CONFIG_EN_MASK) {
        esp32c3_t0_update_counter(t0);
    }

    /* Calculate the new frequency */
    const uint32_t new_divider = FIELD_EX32(value, TIMG_T0CONFIG, DIVIDER);
    const uint64_t new_clk = FIELD_EX32(value, TIMG_T0CONFIG, USE_XTAL) ? ESP32C3_XTAL_CLK : ESP32C3_APB_CLK;
    const uint64_t new_freq = new_clk / new_divider;
    if (new_freq != t0->counter.frequency) {
        t0->counter.frequency = new_freq;
    }

    if (value & R_TIMG_T0CONFIG_DIVCNT_RST_MASK) {
        esp32c3_virtual_counter_reset(&t0->counter);
        esp32c3_t0_alarm_update(t0);
    }

    /* If the alarm state just changed, we have to load it or disable it */
    if (FIELD_CHANGED(former_conf, value, TIMG_T0CONFIG, ALARM_EN)) {
        if (value & R_TIMG_T0CONFIG_ALARM_EN_MASK) {
            esp32c3_t0_alarm_update(t0);
        } else {
            timer_del(&t0->counter.timer);
        }
    }

    /* If the direction of the counter changed, reprogram the alarm. The function esp32c3_t0_alarm_update
     * will check if the counter and alarm are enabled first, no need to do it here. */
    if (FIELD_CHANGED(former_conf, value, TIMG_T0CONFIG, INCREASE)) {
        esp32c3_t0_alarm_update(t0);
    }

    /* Finally, check if the counter state changed */
    if (FIELD_CHANGED(former_conf, value, TIMG_T0CONFIG, EN)) {
        if (value & R_TIMG_T0CONFIG_EN_MASK) {
            /* the counter was disabled, it has just been re-enabled, its value should not be updated,
             * but the base time should be updated to now. */
            esp32c3_virtual_counter_reenabled(&t0->counter);
            esp32c3_t0_alarm_update(t0);
        } else {
            /* In theory, we should update the counter before disabling its timer, but in practice, we
             * already did that at the beginning of this function. Thus, the base time is correct. */
            timer_del(&t0->counter.timer);
        }
    }
}


/**
 * Functions related to the hardware registers
 */
static uint64_t esp32c3_timg_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3TimgState *s = ESP32C3_TIMG(opaque);

    uint64_t r = 0;
    switch (addr) {
        case A_TIMG_RTCCALICFG:
            r = s->rtc.rtc_cali_cfg;
            break;
        case A_TIMG_RTCCALICFG1:
            r = s->rtc.rtc_cali_cfg_result;
            break;
        case A_TIMG_RTCCALICFG2:
            r = s->rtc.rtc_cali_cfg_timeout;
            break;


        /* Timer (T0) related registers */
        case A_TIMG_T0CONFIG:
            r = s->t0.config;
            break;
        case A_TIMG_T0LO:
            r = s->t0.value_flushed & UINT32_MAX;
            break;
        case A_TIMG_T0HI:
            r = s->t0.value_flushed >> 32;
            break;
        case A_TIMG_T0UPDATE:
            /* Write-only register */
            break;
        case A_TIMG_T0ALARMLO:
            r = s->t0.alarm & UINT32_MAX;
            break;
        case A_TIMG_T0ALARMHI:
            r = s->t0.alarm >> 32;
            break;
        case A_TIMG_T0LOADLO:
            r = s->t0.value_toload & UINT32_MAX;
            break;
        case A_TIMG_T0LOADHI:
            r = s->t0.value_toload >> 32;
            break;
        case A_TIMG_T0LOAD:
            /* Write-only register */
            break;


        /* Watchdog related registers */
        case A_TIMG_WDTCONFIG0:
            r = s->wdt.config0;
            break;
        case A_TIMG_WDTCONFIG1:
            r = s->wdt.prescaler_mirror & ~R_TIMG_WDTCONFIG1_DIVCNT_RST_MASK;
            break;
        case A_TIMG_WDTCONFIG2:
        case A_TIMG_WDTCONFIG3:
        case A_TIMG_WDTCONFIG4:
        case A_TIMG_WDTCONFIG5:
            r = s->wdt.stage_mirror[(addr - A_TIMG_WDTCONFIG2) / sizeof(uint32_t)];
            break;
        case A_TIMG_WDTFEED:
            /* This register is read-only, but avoid a warning */
            break;
        case A_TIMG_WDTWPROTECT:
            r = s->wdt.wkey;
            break;

        case A_TIMG_INT_ENA_TIMG:
            r |= s->wdt.int_enabled << R_TIMG_INT_ENA_TIMG_WDT_ENA_SHIFT;
            r |= s->t0.int_enabled << R_TIMG_INT_ENA_TIMG_T0_ENA_SHIFT;
            break;
        case A_TIMG_INT_RAW_TIMG:
            r |= s->wdt.raw_st << R_TIMG_INT_RAW_TIMG_WDT_RAW_SHIFT;
            r |= s->t0.raw_st << R_TIMG_INT_RAW_TIMG_T0_RAW_SHIFT;
            break;
        case A_TIMG_INT_ST_TIMG:
            r |= (s->wdt.int_enabled && s->wdt.raw_st) << R_TIMG_INT_ST_TIMG_WDT_ST_SHIFT;
            r |= (s->t0.int_enabled && s->t0.raw_st) << R_TIMG_INT_ST_TIMG_T0_ST_SHIFT;
            break;

        default:
#if TIMG_WARNING
            warn_report("[TIMG] Unsupported read from %08lx\n", addr);
#endif
            break;
    }

#if TIMG_DEBUG
    info_report("[TIMG] Reading from %08lx (%08lx)\n", addr, r);
#endif
    return r;
}

static void esp32c3_timg_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32C3TimgState *s = ESP32C3_TIMG(opaque);

    switch(addr) {
        case A_TIMG_RTCCALICFG:
            esp32c3_timg_rtc_cali_update(s, value);
            break;
        case A_TIMG_RTCCALICFG2:
            esp32c3_timg_rtc_cali_check_timeout(s, value);
            break;

        /* Timer (T0) related registers */
        case A_TIMG_T0CONFIG:
            esp32c3_t0_config_update(&s->t0, value);
            break;
        case A_TIMG_T0LO:
        case A_TIMG_T0HI:
            /* These registers are read-only but implement them to avoid getting a warning */
            break;
        case A_TIMG_T0UPDATE:
            esp32c3_t0_counter_flush(&s->t0);
            break;
        case A_TIMG_T0ALARMLO:
            s->t0.alarm = load_low(s->t0.alarm, value);
            esp32c3_t0_alarm_update(&s->t0);
            break;
        case A_TIMG_T0ALARMHI:
            s->t0.alarm = load_high(s->t0.alarm, value);
            esp32c3_t0_alarm_update(&s->t0);
            break;
        case A_TIMG_T0LOADLO:
            s->t0.value_toload = load_low(s->t0.value_toload, value);
            break;
        case A_TIMG_T0LOADHI:
            s->t0.value_toload = load_high(s->t0.value_toload, value);
            break;
        case A_TIMG_T0LOAD:
            esp32c3_t0_counter_load(&s->t0);
            break;


        /* Watchdog related registers */
        case A_TIMG_WDTCONFIG0:
            esp32c3_wdt_update_config(&s->wdt, value);
            break;
        case A_TIMG_WDTCONFIG1:
            s->wdt.prescaler_mirror = value;
            break;
        case A_TIMG_WDTCONFIG2:
        case A_TIMG_WDTCONFIG3:
        case A_TIMG_WDTCONFIG4:
        case A_TIMG_WDTCONFIG5:
            s->wdt.stage_mirror[(addr - A_TIMG_WDTCONFIG2) / sizeof(uint32_t)] = value;
            break;
        case A_TIMG_WDTFEED:
            esp32c3_wdt_feed(&s->wdt);
            break;
        case A_TIMG_WDTWPROTECT:
            s->wdt.wkey = value;
            break;

        /* Interrupt related registers */
        case A_TIMG_INT_ENA_TIMG:
            s->wdt.int_enabled = FIELD_EX32(value, TIMG_INT_ENA_TIMG, WDT_ENA) ? true : false;
            s->t0.int_enabled = FIELD_EX32(value, TIMG_INT_ENA_TIMG, T0_ENA) ? true : false;
            break;
        case A_TIMG_INT_CLR_TIMG:
            if (FIELD_EX32(value, TIMG_INT_CLR_TIMG, WDT_CLR)) {
                s->wdt.raw_st = 0;
                qemu_irq_lower(s->wdt.interrupt_irq);
            }
            if (FIELD_EX32(value, TIMG_INT_CLR_TIMG, T0_CLR)) {
                s->t0.raw_st = 0;
                qemu_irq_lower(s->t0.interrupt_irq);
            }
            break;
        case A_TIMG_INT_RAW_TIMG:
        case A_TIMG_INT_ST_TIMG:
            break;

        default:
#if TIMG_WARNING
            warn_report("[TIMG] Unsupported write to %08lx (%08lx)\n", addr, value);
#endif
            break;
    }

#if TIMG_DEBUG
    info_report("[TIMG] Writing to %08lx = %08lx\n", addr, value);
#endif
}


static const MemoryRegionOps esp32c3_timg_ops = {
    .read =  esp32c3_timg_read,
    .write = esp32c3_timg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp32c3_timg_reset(DeviceState* ts)
{
    ESP32C3TimgState *s = ESP32C3_TIMG(ts);

    /* Reset watchdog */
    esp32c3_virtual_counter_reset(&s->wdt.counter);
    s->wdt.config0 = 0;
    s->wdt.wkey = ESP32C3_WDT_DEFAULT_WKEY;
    s->wdt.current_stage = 0;
    memset(&s->wdt.stage_conf, 0, sizeof(s->wdt.stage_conf));
    s->wdt.stage[0] = 26000000;
    s->wdt.stage[1] = 0x7FFFFFFF;
    s->wdt.stage[2] = 0x0FFFFFFF;
    s->wdt.stage[3] = 0x0FFFFFFF;
    s->wdt.prescaler = 1;
    s->wdt.raw_st = 0;
    s->wdt.int_enabled = 0;

    /* Reset Timer0 */
    esp32c3_virtual_counter_reset(&s->t0.counter);
    s->t0.raw_st = 0;
    s->t0.int_enabled = 0;
    s->t0.value_rel = 0;
    /* Set the divider to 1 */
    s->t0.config = 1 << R_TIMG_T0CONFIG_DIVIDER_SHIFT;
}


static void esp32c3_timg_realize(DeviceState *dev, Error **errp)
{
}


static void esp32c3_timg_init(Object *obj)
{
    ESP32C3TimgState *s = ESP32C3_TIMG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_timg_ops, s,
                          TYPE_ESP32C3_TIMG, ESP32C3_TIMG_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Set default value to calibration register */
    s->rtc.rtc_cali_cfg = 1 << R_TIMG_RTCCALICFG_MAX_SHIFT |
                         1 << R_TIMG_RTCCALICFG_CLK_SEL_SHIFT |
                         1 << R_TIMG_RTCCALICFG_START_CYCLING_SHIFT;

    /* Watchdog initialization */
    s->wdt.wkey = ESP32C3_WDT_DEFAULT_WKEY;
    qdev_init_gpio_out_named(DEVICE(sbd), &s->wdt.reset_irq, ESP32C3_WDT_IRQ_RESET, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->wdt.interrupt_irq, ESP32C3_WDT_IRQ_INTERRUPT, 1);
    timer_init_ns(esp32c3_virtual_counter_get_timer(&s->wdt.counter), QEMU_CLOCK_VIRTUAL, esp32c3_wdt_cb, &s->wdt);

    /* Timer T0 initialization */
    qdev_init_gpio_out_named(DEVICE(sbd), &s->t0.interrupt_irq, ESP32C3_T0_IRQ_INTERRUPT, 1);
    timer_init_ns(esp32c3_virtual_counter_get_timer(&s->t0.counter), QEMU_CLOCK_VIRTUAL, esp32c3_t0_cb, &s->t0);

    /* Set the initial values for the internal fields */
    esp32c3_timg_reset((DeviceState*) s);
}

static Property esp32c3_timg_properties[] = {
    // DEFINE_PROP_BOOL("wdt_disable", ESP32C3TimgState, wdt_disable, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c3_timg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_timg_reset;
    dc->realize = esp32c3_timg_realize;
    device_class_set_props(dc, esp32c3_timg_properties);
}

static const TypeInfo esp32c3_timg_info = {
    .name = TYPE_ESP32C3_TIMG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3TimgState),
    .instance_init = esp32c3_timg_init,
    .class_init = esp32c3_timg_class_init
};

static void esp32c3_timg_register_types(void)
{
    type_register_static(&esp32c3_timg_info);
}

type_init(esp32c3_timg_register_types)

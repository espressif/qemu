/*
 * Timer Group emulation for recent ESP32-series chip (ESP32-S3 and newer)
 *
 * Copyright (c) 2023-2025 Espressif Systems (Shanghai) Co. Ltd.
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
#include "hw/timer/esp_timg.h"

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
static int64_t esp_virtual_counter_update(ESPVirtualCounter *counter)
{
    const int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const int64_t elapsed_ns = now - counter->base;
    const int64_t ticks = (elapsed_ns * (counter->frequency / 1000)) / 1000000;
    counter->value += ticks;
    counter->base = now;
    return counter->value;
}

static inline QEMUTimer* esp_virtual_counter_get_timer(ESPVirtualCounter *counter)
{
    return &counter->timer;
}

static void esp_virtual_counter_alarm_in_ticks(ESPVirtualCounter *counter, int64_t ticks)
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
static void esp_virtual_counter_reenabled(ESPVirtualCounter *counter)
{
    counter->base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void esp_virtual_counter_reset(ESPVirtualCounter* counter)
{
    timer_del(&counter->timer);
    counter->base = 0;
    counter->value = 0;
    counter->frequency = ESP_APB_CLK; // Hz
}


/**
 * @brief Function called when an update of the RTC Calibration register is requested
 *        Perform the required calibration simulation here and update the register.
 */
static void esp_timg_rtc_cali_update(ESPTimgState *s, uint32_t value)
{
    const uint32_t osc_freq_arr[] = {
        [ESP_TIMG_CALI_RC_SLOW_CLK]     = ESP_RC_SLOW_FREQ,
        [ESP_TIMG_CALI_RC_FAST_DIV_CLK] = ESP_RC_FAST_DIV_FREQ,
        [ESP_TIMG_CALI_XTAL32K_CLK]     = ESP_XTAL32K_FREQ
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
        const uint32_t xtal_count = (ESP_XTAL_CLK * max_count) / freq;

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
static void esp_timg_rtc_cali_check_timeout(ESPTimgState *s, uint32_t value)
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

static inline uint64_t esp_wdt_ext_clk_frequency(ESPWdtState* wdt)
{
    return FIELD_EX32(wdt->config0, TIMG_WDTCONFIG0, USE_XTAL) ? ESP_XTAL_CLK : ESP_APB_CLK;
}

static inline bool esp_wdt_is_writable(ESPWdtState* wdt)
{
    return wdt->wkey == ESP_WDT_DEFAULT_WKEY;
}

static inline bool esp_wdt_enabled(ESPWdtState* wdt)
{
    return FIELD_EX32(wdt->config0, TIMG_T0CONFIG, EN) ? 1 : 0;
}


static void esp_wdt_cb(void* opaque)
{
    ESPWdtState* wdt = (ESPWdtState*) opaque;
    const int cur_stage = wdt->current_stage;
    ESPWdtStageConf conf = wdt->stage_conf[cur_stage];

    /* Retrieve the `wdt_disable` property */
    ESPTimgState* state = container_of(opaque, ESPTimgState, wdt);

    if (state->wdt_disable) {
        return;
    }

    /* Check which action must be taken for the current stage */
    if (conf == ESP_WDT_INTERRUPT) {
        wdt->raw_st = 1;
        if (wdt->int_enabled) {
            qemu_irq_raise(wdt->interrupt_irq);
        }
    } else if (conf == ESP_WDT_RESET_CPU || conf == ESP_WDT_RESET_SYS) {
        qemu_irq_raise(wdt->reset_irq);
        /* Do not schedule anything if we have to reset the machine */
        return;
    }

    const int new_stage = (cur_stage + 1)  % ESP_WDT_STAGE_COUNT;
    wdt->current_stage = new_stage;

    if (conf == ESP_WDT_OFF) {
        /* If the current stage is disabled, the counter shall not be reset to 0!
         * Get the number of ticks elapsed to calculate the remaining ticks before the next stage alarm.
         * A simpler option would be to reuse wdt->stage[cur_stage], but if the application modified this
         * register after scheduling an alarm, the result would be undefined. */
        const int64_t elapsed = esp_virtual_counter_update(&wdt->counter);
        esp_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[new_stage] - elapsed);
    } else {
        esp_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[new_stage]);
    }
}

static void esp_wdt_update_prescaler(ESPWdtState* wdt, uint32_t value)
{
    if (FIELD_EX32(value, TIMG_WDTCONFIG1, DIVCNT_RST) || value == 0) {
        /* Avoid any divide-by-0 error in the code below */
        wdt->prescaler = 1;
    } else {
        wdt->prescaler = FIELD_EX32(value, TIMG_WDTCONFIG1, CLK_PRESCALE);
    }

    /* Recalculate the frequency out of the new prescaler and current clock */
    wdt->counter.frequency = esp_wdt_ext_clk_frequency(wdt) / wdt->prescaler;

    /* In theory we should reschedule the timer if it is currently running.
     * In practice, let's say that this behavior is invalid  and do not reschedule it. */
}

static void esp_wdt_update_stage(ESPWdtState* wdt, int index, uint32_t value, bool verify)
{
    wdt->stage[index] = value;

    /* If the updated stage is the current one and the watchdog is enabled, reprogram the timer */
    if (esp_wdt_enabled(wdt) && wdt->current_stage == index && verify) {
        /* Update the counter of the running timer, so that we can adjust the alarm */
        int64_t counter_value = esp_virtual_counter_update(&wdt->counter);
        int64_t diff = (int64_t) value - counter_value;
        if (diff <= 0) {
            /* On the real hardware, the WDT is simply disabled if the new comparator value for the current
             * stage is smaller than the current value. It will be restarted (not resumed) when fed.
             * Just like the real hardware, keep the "enable" bit to 1, moreover it is required for feeding.
             */
            timer_del(&wdt->counter.timer);
        } else {
            /* The new alarm is set to happen in `diff` ticks, reschedule the alarm */
            esp_virtual_counter_alarm_in_ticks(&wdt->counter, diff);
        }
    }
}

static void esp_wdt_feed(ESPWdtState* wdt)
{
    if (esp_wdt_enabled(wdt)) {
        wdt->current_stage = 0;
        esp_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[0]);
    }
}

static void esp_wdt_update_config(ESPWdtState* wdt, uint32_t value)
{
    /* If the WDT is protected return */
    if (!esp_wdt_is_writable(wdt)) {
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
            esp_wdt_update_prescaler(wdt, wdt->prescaler_mirror);
        }

        /* Update the stage configuration mirror */
        wdt->stage_conf[0] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG0);
        wdt->stage_conf[1] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG1);
        wdt->stage_conf[2] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG2);
        wdt->stage_conf[3] = FIELD_EX32(value, TIMG_WDTCONFIG0, STG3);

        /* Update the stage values */
        for (int i = 0; i < ESP_WDT_STAGE_COUNT; i++) {
            /* Only reprogram the timer if the enable flag didn't change */
            esp_wdt_update_stage(wdt, i, wdt->stage_mirror[i], enabled && !enabled_changed);
        }
    }

    /* Check if the enabled bit changed */
    if (enabled_changed) {
        if (enabled) {
            wdt->config0 |= R_TIMG_WDTCONFIG0_EN_MASK;
            /* Timer has just been (re-)enabled, schedule the timer */
            esp_virtual_counter_alarm_in_ticks(&wdt->counter, wdt->stage[0]);
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
static void esp_t0_update_counter(ESPT0State* t)
{
    int64_t previous = t->counter.value;
    int64_t current = esp_virtual_counter_update(&t->counter);
    int64_t delta = current - previous;
    const bool increase = FIELD_EX32(t->config, TIMG_T0CONFIG, INCREASE) ? true : false;
    if (increase) {
        t->value_rel = (t->value_rel + delta) & ESP_TIMG_T0_MAX_VALUE;
    } else {
        t->value_rel = (t->value_rel - delta) & ESP_TIMG_T0_MAX_VALUE;
    }
}


static void esp_t0_cb(void* opaque)
{
    ESPT0State* t = (ESPT0State*) opaque;

    /* Disable the alarm timer */
    timer_del(&t->counter.timer);
    esp_virtual_counter_reenabled(&t->counter);

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
    t->raw_st = 1;
    if (t->int_enabled) {
        qemu_irq_raise(t->interrupt_irq);
    }
}


static void esp_t0_counter_flush(ESPT0State* t)
{
    if (FIELD_EX32(t->config, TIMG_T0CONFIG, EN)) {
        esp_t0_update_counter(t);
    }

    t->value_flushed = t->value_rel;
}


static void esp_t0_alarm_update(ESPT0State* t)
{

    if (FIELD_EX32(t->config, TIMG_T0CONFIG, EN) &&
        FIELD_EX32(t->config, TIMG_T0CONFIG, ALARM_EN)) {

        const bool increase = FIELD_EX32(t->config, TIMG_T0CONFIG, INCREASE) ? true : false;
        const bool decrease = !increase;
        const uint64_t alarm = t->alarm;

        /* Update the current value of the relative counter */
        esp_t0_update_counter(t);

        /* No matter if we increase or decrease the counter the time difference is the same */
        const uint64_t value = t->value_rel;
        uint64_t diff = (alarm > value) ? alarm - value : value - alarm;
        const uint64_t limit = ESP_TIMG_T0_LIMIT;

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
            esp_t0_cb(t);
        } else if ((increase && scenario2) || (decrease && scenario6)) {
            /* The alarm is in range and in the future, program its trigger */
            esp_virtual_counter_alarm_in_ticks(&t->counter, diff);
        } else {
            assert(scenario4 || scenario8);
            /* The alarm is in range, in the future, but requires the timer to overflow/underflow */
            const uint64_t high = MAX(alarm, value);
            const uint64_t low  = MIN(alarm, value);
            /* Calculate the new (tick) difference between them */
            diff = (ESP_TIMG_T0_MAX_VALUE + 1 - high) + low;
            esp_virtual_counter_alarm_in_ticks(&t->counter, diff);
        }

    }
}


static void esp_t0_counter_load(ESPT0State* t)
{
    /* Update the counter so that the (time) base is up to date */
    esp_t0_update_counter(t);

    /* Set the new counter */
    t->value_rel = t->value_toload;

    /* Reprogram the alarm if necessary */
    esp_t0_alarm_update(t);
}

static void esp_t0_config_update(ESPT0State* t0, uint32_t value)
{
    const uint32_t former_conf = t0->config;
    /* Assign the new configuration while removing the write-only bits */
    t0->config = value & ~(R_TIMG_T0CONFIG_DIVCNT_RST_MASK);

    /* If the counter was enabled until now, update its value */
    if (former_conf & R_TIMG_T0CONFIG_EN_MASK) {
        esp_t0_update_counter(t0);
    }

    /* Calculate the new frequency, a divider of 0 means 65536 */
    uint32_t new_divider = FIELD_EX32(value, TIMG_T0CONFIG, DIVIDER);
    if (new_divider == 0) {
        new_divider = 65536;
    }
    const uint64_t new_clk = FIELD_EX32(value, TIMG_T0CONFIG, USE_XTAL) ? ESP_XTAL_CLK : ESP_APB_CLK;
    const uint64_t new_freq = new_clk / new_divider;
    if (new_freq != t0->counter.frequency) {
        t0->counter.frequency = new_freq;
    }

    if (value & R_TIMG_T0CONFIG_DIVCNT_RST_MASK) {
        esp_virtual_counter_reset(&t0->counter);
        esp_t0_alarm_update(t0);
    }

    /* If the alarm state just changed, we have to load it or disable it */
    if (FIELD_CHANGED(former_conf, value, TIMG_T0CONFIG, ALARM_EN)) {
        if (value & R_TIMG_T0CONFIG_ALARM_EN_MASK) {
            esp_t0_alarm_update(t0);
        } else {
            timer_del(&t0->counter.timer);
        }
    }

    /* If the direction of the counter changed, reprogram the alarm. The function esp_t0_alarm_update
     * will check if the counter and alarm are enabled first, no need to do it here. */
    if (FIELD_CHANGED(former_conf, value, TIMG_T0CONFIG, INCREASE)) {
        esp_t0_alarm_update(t0);
    }

    /* Finally, check if the counter state changed */
    if (FIELD_CHANGED(former_conf, value, TIMG_T0CONFIG, EN)) {
        if (value & R_TIMG_T0CONFIG_EN_MASK) {
            /* the counter was disabled, it has just been re-enabled, its value should not be updated,
             * but the base time should be updated to now. */
            esp_virtual_counter_reenabled(&t0->counter);
            esp_t0_alarm_update(t0);
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
static uint64_t esp_timg_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPTimgState *s = ESP_TIMG(opaque);
    ESPT0State   *t = &s->t0;
    ESPTimgClass *klass = ESP_TIMG_GET_CLASS(s);
    uint32_t      wdt_shift = R_TIMG_T0_INT_TIMG_WDT_RAW_SHIFT;
    uint32_t      t0_shift  = R_TIMG_T0_INT_TIMG_T0_RAW_SHIFT;
    uint32_t      t1_shift  = R_TIMG_T0_INT_TIMG_T1_RAW_SHIFT;

    /* Check if the timer to change/configure is the first or the second one, of course, this only applies
     * to targets who have two timers */
    if (klass->m_has_t1 && addr >= A_TIMG_T1CONFIG && addr <= A_TIMG_T1LOAD) {
        /* Shifting the registers works because the T0 and T1 configuration registers directly follow each other */
        addr -= A_TIMG_T1CONFIG;
        t = &s->t1;
    }

    /**
     * On targets that have both T0 and T1, the interrupt registers lowest 3 bits are organized as:
     * WDT_bit | T1_bit  | T0_bit
     * On other targets, they are organized as:
     *    0    | WDT_bit | T0_bit
     **/
    if (klass->m_has_t1) {
        wdt_shift = R_TIMG_T0T1_INT_TIMG_WDT_RAW_SHIFT;
        t0_shift  = R_TIMG_T0T1_INT_TIMG_T0_RAW_SHIFT;
        t1_shift  = R_TIMG_T0T1_INT_TIMG_T1_RAW_SHIFT;
    }

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
            r = t->config;
            break;
        case A_TIMG_T0LO:
            r = t->value_flushed & UINT32_MAX;
            break;
        case A_TIMG_T0HI:
            r = t->value_flushed >> 32;
            break;
        case A_TIMG_T0UPDATE:
            /* Write-only register */
            break;
        case A_TIMG_T0ALARMLO:
            r = t->alarm & UINT32_MAX;
            break;
        case A_TIMG_T0ALARMHI:
            r = t->alarm >> 32;
            break;
        case A_TIMG_T0LOADLO:
            r = t->value_toload & UINT32_MAX;
            break;
        case A_TIMG_T0LOADHI:
            r = t->value_toload >> 32;
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

        /**
         * On targets that have both T0 and T1, the interrupt registers lowest 3 bits are organized as:
         * WDT_bit | T1_bit  | T0_bit
         * On other targets, they are organized as:
         *    0    | WDT_bit | T0_bit
         **/
        case A_TIMG_INT_ENA_TIMG:
            r = s->wdt.int_enabled << wdt_shift |
                s->t1.int_enabled  << t1_shift  |
                s->t0.int_enabled  << t0_shift;
            break;
        case A_TIMG_INT_RAW_TIMG:
            r = s->wdt.raw_st << wdt_shift |
                s->t1.raw_st  << t1_shift  |
                s->t0.raw_st  << t0_shift;
            break;
        case A_TIMG_INT_ST_TIMG:
            r = (s->wdt.int_enabled && s->wdt.raw_st) << wdt_shift |
                (s->t1.int_enabled  && s->t1.raw_st)  << t1_shift  |
                (s->t0.int_enabled  && s->t0.raw_st)  << t0_shift;
            break;

        default:
#if TIMG_WARNING
            warn_report("[TIMG] Unsupported read from %08lx", addr);
#endif
            break;
    }

#if TIMG_DEBUG
    info_report("[TIMG] Reading from %08lx (%08lx)", addr, r);
#endif
    return r;
}

static void esp_timg_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESPTimgState *s = ESP_TIMG(opaque);
    ESPT0State   *t = &s->t0;
    ESPTimgClass *klass = ESP_TIMG_GET_CLASS(s);
    uint32_t      wdt_mask = R_TIMG_T0_INT_TIMG_WDT_RAW_MASK;
    uint32_t      t0_mask  = R_TIMG_T0_INT_TIMG_T0_RAW_MASK;
    uint32_t      t1_mask  = R_TIMG_T0_INT_TIMG_T1_RAW_MASK;

    /* Check if the timer to change/configure is the first or the second one, of course, this only applies
     * to targets who have two timers */
    if (klass->m_has_t1 && addr >= A_TIMG_T1CONFIG && addr <= A_TIMG_T1LOAD) {
        /* Shifting the registers works because the T0 and T1 configuration registers directly follow each other */
        addr -= A_TIMG_T1CONFIG;
        t = &s->t1;
    }

    /**
     * On targets that have both T0 and T1, the interrupt registers lowest 3 bits are organized as:
     * WDT_bit | T1_bit  | T0_bit
     * On other targets, they are organized as:
     *    0    | WDT_bit | T0_bit
     **/
    if (klass->m_has_t1) {
        wdt_mask = R_TIMG_T0T1_INT_TIMG_WDT_RAW_MASK;
        t0_mask  = R_TIMG_T0T1_INT_TIMG_T0_RAW_MASK;
        t1_mask  = R_TIMG_T0T1_INT_TIMG_T1_RAW_MASK;
    }


    switch(addr) {
        case A_TIMG_RTCCALICFG:
            esp_timg_rtc_cali_update(s, value);
            break;
        case A_TIMG_RTCCALICFG2:
            esp_timg_rtc_cali_check_timeout(s, value);
            break;

        /* Timer (T0) related registers */
        case A_TIMG_T0CONFIG:
            esp_t0_config_update(t, value);
            break;
        case A_TIMG_T0LO:
        case A_TIMG_T0HI:
            /* These registers are read-only but implement them to avoid getting a warning */
            break;
        case A_TIMG_T0UPDATE:
            esp_t0_counter_flush(t);
            break;
        case A_TIMG_T0ALARMLO:
            t->alarm = load_low(t->alarm, value);
            esp_t0_alarm_update(t);
            break;
        case A_TIMG_T0ALARMHI:
            t->alarm = load_high(t->alarm, value);
            esp_t0_alarm_update(t);
            break;
        case A_TIMG_T0LOADLO:
            t->value_toload = load_low(t->value_toload, value);
            break;
        case A_TIMG_T0LOADHI:
            t->value_toload = load_high(t->value_toload, value);
            break;
        case A_TIMG_T0LOAD:
            esp_t0_counter_load(t);
            break;


        /* Watchdog related registers */
        case A_TIMG_WDTCONFIG0:
            esp_wdt_update_config(&s->wdt, value);
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
            esp_wdt_feed(&s->wdt);
            break;
        case A_TIMG_WDTWPROTECT:
            s->wdt.wkey = value;
            break;
        case A_TIMG_INT_ENA_TIMG: {
            bool former = s->t0.int_enabled;
            s->t0.int_enabled  = (value & t0_mask)  ? true : false;
            if (s->t0.int_enabled != former) {
                qemu_set_irq(s->t0.interrupt_irq,
                             s->t0.raw_st && s->t0.int_enabled ? 1 : 0);
            }

            former = s->t1.int_enabled;
            s->t1.int_enabled  = (value & t1_mask)  ? true : false;
            if (s->t1.int_enabled != former) {
                qemu_set_irq(s->t1.interrupt_irq,
                             s->t1.raw_st && s->t1.int_enabled ? 1 : 0);
            }

            former = s->wdt.int_enabled;
            s->wdt.int_enabled = (value & wdt_mask) ? true : false;
            if (s->wdt.int_enabled != former) {
                qemu_set_irq(s->wdt.interrupt_irq,
                             s->wdt.raw_st && s->wdt.int_enabled ? 1 : 0);
            }
            break;
        }
        case A_TIMG_INT_CLR_TIMG:
            if (value & t0_mask) {
                s->t0.raw_st = 0;
                qemu_irq_lower(s->t0.interrupt_irq);
            }
            if (value & t1_mask) {
                s->t1.raw_st = 0;
                qemu_irq_lower(s->t1.interrupt_irq);
            }
            if (value & wdt_mask) {
                s->wdt.raw_st = 0;
                qemu_irq_lower(s->wdt.interrupt_irq);
            }
            break;
        case A_TIMG_INT_RAW_TIMG:
        case A_TIMG_INT_ST_TIMG:
            break;

        default:
#if TIMG_WARNING
            warn_report("[TIMG] Unsupported write to %08lx (%08lx)", addr, value);
#endif
            break;
    }

#if TIMG_DEBUG
    info_report("[TIMG] Writing to %08lx = %08lx", addr, value);
#endif
}


static const MemoryRegionOps esp_timg_ops = {
    .read =  esp_timg_read,
    .write = esp_timg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp_timg_reset_hold(Object *obj, ResetType type)
{
    ESPTimgState *s = ESP_TIMG(obj);

    /* Reset watchdog */
    esp_virtual_counter_reset(&s->wdt.counter);
    s->wdt.config0 = 0;
    s->wdt.wkey = ESP_WDT_DEFAULT_WKEY;
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
    esp_virtual_counter_reset(&s->t0.counter);
    s->t0.raw_st = 0;
    s->t0.int_enabled = 0;
    s->t0.value_rel = 0;
    /* Set the divider to 1 */
    s->t0.config = 1 << R_TIMG_T0CONFIG_DIVIDER_SHIFT;

    /* Reset Timer1 even if the target doesn't have T1 */
    esp_virtual_counter_reset(&s->t1.counter);
    s->t1.raw_st = 0;
    s->t1.int_enabled = 0;
    s->t1.value_rel = 0;
    s->t1.config = 1 << R_TIMG_T0CONFIG_DIVIDER_SHIFT;
}


static void esp_timg_realize(DeviceState *dev, Error **errp)
{
}


static void esp_timg_init(Object *obj)
{
    ESPTimgState *s = ESP_TIMG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ESPTimgClass *klass = ESP_TIMG_GET_CLASS(s);

    memory_region_init_io(&s->iomem, obj, &esp_timg_ops, s,
                          TYPE_ESP_TIMG, ESP_TIMG_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Set default value to calibration register */
    s->rtc.rtc_cali_cfg = 1 << R_TIMG_RTCCALICFG_MAX_SHIFT |
                         1 << R_TIMG_RTCCALICFG_CLK_SEL_SHIFT |
                         1 << R_TIMG_RTCCALICFG_START_CYCLING_SHIFT;

    /* Watchdog initialization */
    s->wdt.wkey = ESP_WDT_DEFAULT_WKEY;
    qdev_init_gpio_out_named(DEVICE(sbd), &s->wdt.reset_irq, ESP_WDT_IRQ_RESET, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->wdt.interrupt_irq, ESP_WDT_IRQ_INTERRUPT, 1);
    timer_init_ns(esp_virtual_counter_get_timer(&s->wdt.counter), QEMU_CLOCK_VIRTUAL, esp_wdt_cb, &s->wdt);

    /* Timer T0 initialization */
    qdev_init_gpio_out_named(DEVICE(sbd), &s->t0.interrupt_irq, ESP_T0_IRQ_INTERRUPT, 1);
    timer_init_ns(esp_virtual_counter_get_timer(&s->t0.counter), QEMU_CLOCK_VIRTUAL, esp_t0_cb, &s->t0);

    /* Only initialize the timer T1 interrupt if the target supports it */
    if (klass->m_has_t1) {
        qdev_init_gpio_out_named(DEVICE(sbd), &s->t1.interrupt_irq, ESP_T1_IRQ_INTERRUPT, 1);
        timer_init_ns(esp_virtual_counter_get_timer(&s->t1.counter), QEMU_CLOCK_VIRTUAL, esp_t0_cb, &s->t1);
    }

    /* Set the initial values for the internal fields */
    esp_timg_reset_hold(obj, RESET_TYPE_COLD);
}

static Property esp_timg_properties[] = {
    DEFINE_PROP_BOOL("wdt_disable", ESPTimgState, wdt_disable, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp_timg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp_timg_reset_hold;
    dc->realize = esp_timg_realize;
    device_class_set_props(dc, esp_timg_properties);
}

static const TypeInfo esp_timg_info = {
    .name = TYPE_ESP_TIMG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPTimgState),
    .instance_init = esp_timg_init,
    .class_init = esp_timg_class_init,
    .class_size = sizeof(ESPTimgClass),
    .abstract = true,
};

static void esp_timg_register_types(void)
{
    type_register_static(&esp_timg_info);
}

type_init(esp_timg_register_types)

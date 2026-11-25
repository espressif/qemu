/*
 * ESP32 RTC_CNTL (RTC block controller) device
 *
 * Copyright (c) 2019-2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/esp32s3_reg.h"
#include "hw/misc/esp32s3_rtc_cntl.h"

static void esp32s3_rtc_update_cpu_stall(Esp32s3RtcCntlState* s);
static void esp32s3_rtc_sleep_begin(Esp32s3RtcCntlState *s);
static void esp32s3_rtc_update_clk(Esp32s3RtcCntlState* s);

static uint64_t esp32s3_rtc_cntl_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32s3RtcCntlState *s = ESP32S3_RTC_CNTL(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_RTC_CNTL_OPTIONS0:
        r = s->options0_reg;
        break;
    case A_RTC_CNTL_TIME_UPDATE:
        r = R_RTC_CNTL_TIME_UPDATE_VALID_MASK;
        break;
    case A_RTC_CNTL_TIME0:
        r = s->time_reg & UINT32_MAX;
        break;
    case A_RTC_CNTL_TIME1:
        r = s->time_reg >> 32;
        break;

    case A_RTC_CNTL_RESET_STATE:
        r = FIELD_DP32(r, RTC_CNTL_RESET_STATE, RESET_CAUSE_PROCPU, s->reset_cause[0]);
        r = FIELD_DP32(r, RTC_CNTL_RESET_STATE, RESET_CAUSE_APPCPU, s->reset_cause[1]);
        r = FIELD_DP32(r, RTC_CNTL_RESET_STATE, PROCPU_STAT_VECTOR_SEL, s->stat_vector_sel[0]);
        r = FIELD_DP32(r, RTC_CNTL_RESET_STATE, APPCPU_STAT_VECTOR_SEL, s->stat_vector_sel[1]);
        break;

    case A_RTC_CNTL_STORE0:
    case A_RTC_CNTL_STORE1:
    case A_RTC_CNTL_STORE2:
    case A_RTC_CNTL_STORE3:
        r = s->scratch_reg[(addr - A_RTC_CNTL_STORE0) / 4];
        break;

    case A_RTC_CNTL_CLK_CONF:
        r = FIELD_DP32(r, RTC_CNTL_CLK_CONF, SOC_CLK_SEL, s->soc_clk);
        r = FIELD_DP32(r, RTC_CNTL_CLK_CONF, FAST_CLK_RTC_SEL, s->rtc_fastclk);
        r = FIELD_DP32(r, RTC_CNTL_CLK_CONF, ANA_CLK_RTC_SEL, s->rtc_slowclk);
        break;

    case A_RTC_CNTL_SW_CPU_STALL:
        r = s->sw_cpu_stall_reg;
        break;

    case A_RTC_CNTL_STORE4:
    case A_RTC_CNTL_STORE5:
    case A_RTC_CNTL_STORE6:
    case A_RTC_CNTL_STORE7:
        r = s->scratch_reg[(addr - A_RTC_CNTL_STORE4) / 4 + 4];
        break;

    case A_RTC_CNTL_SLP_TIMER0:
    case A_RTC_CNTL_SLP_TIMER1:
        r = s->slp_timer_reg[(addr - A_RTC_CNTL_SLP_TIMER0) / 4];
        break;
    case A_RTC_CNTL_STATE0:
        r = s->state0_reg;
        break;
    case A_RTC_CNTL_WAKEUP_STATE:
        r = s->wakeup_state_reg;
        break;
    case A_RTC_CNTL_INT_ENA:
        r = s->int_ena_reg;
        break;
    case A_RTC_CNTL_INT_RAW:
        r = s->int_raw_reg;
        break;
    case A_RTC_CNTL_INT_ST:
        r = s->int_raw_reg & s->int_ena_reg;
        break;
    case A_RTC_CNTL_SLP_REJECT_CONF:
        r = s->slp_reject_conf_reg;
        break;
    case A_RTC_CNTL_SLP_REJECT_CAUSE:
        r = s->slp_reject_cause;
        break;
    case A_RTC_CNTL_SLP_WAKEUP_CAUSE:
        r = s->slp_wakeup_cause;
        break;
    }
    return r;
}

static void esp32s3_rtc_cntl_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size)
{
    Esp32s3RtcCntlState *s = ESP32S3_RTC_CNTL(opaque);
    switch (addr) {
    case A_RTC_CNTL_OPTIONS0:
        if (value & R_RTC_CNTL_OPTIONS0_SW_SYS_RESET_MASK) {
            s->reset_cause[0] = ESP32_SW_SYS_RESET;
            s->reset_cause[1] = ESP32_SW_SYS_RESET;
            qemu_irq_pulse(s->dig_reset_req);
            value &= ~(R_RTC_CNTL_OPTIONS0_SW_SYS_RESET_MASK);
        }
        if (value & R_RTC_CNTL_OPTIONS0_SW_APPCPU_RESET_MASK) {
            s->reset_cause[1] = ESP32_SW_CPU_RESET;
            qemu_irq_pulse(s->cpu_reset_req[1]);
            value &= ~(R_RTC_CNTL_OPTIONS0_SW_APPCPU_RESET_MASK);
        }
        if (value & R_RTC_CNTL_OPTIONS0_SW_PROCPU_RESET_MASK) {
            s->reset_cause[0] = ESP32_SW_CPU_RESET;
            qemu_irq_pulse(s->cpu_reset_req[0]);
            value &= ~(R_RTC_CNTL_OPTIONS0_SW_PROCPU_RESET_MASK);
        }
        s->options0_reg = value;
        esp32s3_rtc_update_cpu_stall(s);
        break;

    case A_RTC_CNTL_TIME_UPDATE:
        if (value & R_RTC_CNTL_TIME_UPDATE_UPDATE_MASK) {
            s->time_reg = muldiv64(
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->time_base_ns,
                s->rtc_slowclk_freq, NANOSECONDS_PER_SECOND);
        }
        break;

    case A_RTC_CNTL_RESET_STATE:
        s->stat_vector_sel[0] = FIELD_EX32(value, RTC_CNTL_RESET_STATE,
                                           PROCPU_STAT_VECTOR_SEL);
        s->stat_vector_sel[1] = FIELD_EX32(value, RTC_CNTL_RESET_STATE,
                                           APPCPU_STAT_VECTOR_SEL);
        break;

    case A_RTC_CNTL_STORE0:
    case A_RTC_CNTL_STORE1:
    case A_RTC_CNTL_STORE2:
    case A_RTC_CNTL_STORE3:
        s->scratch_reg[(addr - A_RTC_CNTL_STORE0) / 4] = value;
        break;

    case A_RTC_CNTL_CLK_CONF:
        s->soc_clk = FIELD_EX32(value, RTC_CNTL_CLK_CONF, SOC_CLK_SEL);
        s->rtc_fastclk = FIELD_EX32(value, RTC_CNTL_CLK_CONF, FAST_CLK_RTC_SEL);
        s->rtc_slowclk = FIELD_EX32(value, RTC_CNTL_CLK_CONF, ANA_CLK_RTC_SEL);
        esp32s3_rtc_update_clk(s);
        break;

    case A_RTC_CNTL_SW_CPU_STALL:
        s->sw_cpu_stall_reg = value;
        esp32s3_rtc_update_cpu_stall(s);
        break;

    case A_RTC_CNTL_STORE4:
    case A_RTC_CNTL_STORE5:
    case A_RTC_CNTL_STORE6:
    case A_RTC_CNTL_STORE7:
        s->scratch_reg[(addr - A_RTC_CNTL_STORE4) / 4 + 4] = value;
        break;

    case A_RTC_CNTL_SLP_TIMER0:
    case A_RTC_CNTL_SLP_TIMER1:
        s->slp_timer_reg[(addr - A_RTC_CNTL_SLP_TIMER0) / 4] = value;
        break;
    case A_RTC_CNTL_STATE0:
        if (FIELD_EX32(value, RTC_CNTL_STATE0, SLP_REJECT_CAUSE_CLR)) {
            s->slp_reject_cause = 0;
        }
        /* SLP_REJECT and SLP_WAKEUP are status, set by the hardware. */
        s->state0_reg = (s->state0_reg & (R_RTC_CNTL_STATE0_SLP_REJECT_MASK |
                                          R_RTC_CNTL_STATE0_SLP_WAKEUP_MASK)) |
                        (value & ~(R_RTC_CNTL_STATE0_SLP_REJECT_MASK |
                                   R_RTC_CNTL_STATE0_SLP_WAKEUP_MASK |
                                   R_RTC_CNTL_STATE0_SLP_REJECT_CAUSE_CLR_MASK));
        if (FIELD_EX32(value, RTC_CNTL_STATE0, SLEEP_EN) && !s->asleep) {
            esp32s3_rtc_sleep_begin(s);
        }
        break;
    case A_RTC_CNTL_WAKEUP_STATE:
        s->wakeup_state_reg = value;
        break;
    case A_RTC_CNTL_INT_ENA:
        s->int_ena_reg = value;
        break;
    case A_RTC_CNTL_INT_CLR:
        s->int_raw_reg &= ~value;
        break;
    case A_RTC_CNTL_SLP_REJECT_CONF:
        s->slp_reject_conf_reg = value;
        break;
    }
}

/*
 * Light sleep. ESP-IDF's rtc_sleep_start() sets STATE0.SLEEP_EN and waits for
 * INT_RAW to report SLP_WAKEUP or SLP_REJECT. Wake sources are numbered as in
 * soc/rtc.h; the RTC timer and the GPIOs are modelled. The cores are not
 * stopped: the firmware's own wait loop runs meanwhile.
 */
#define RTC_TRIG_GPIO  (1u << 2)    /* RTC_GPIO_TRIG_EN */
#define RTC_TRIG_TIMER (1u << 3)    /* RTC_TIMER_TRIG_EN */

static uint64_t esp32s3_rtc_now_ticks(Esp32s3RtcCntlState *s)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->time_base_ns,
                    s->rtc_slowclk_freq, NANOSECONDS_PER_SECOND);
}

static uint64_t esp32s3_rtc_target_ticks(Esp32s3RtcCntlState *s)
{
    return ((uint64_t)FIELD_EX32(s->slp_timer_reg[1], RTC_CNTL_SLP_TIMER1,
                                 SLP_VAL_HI) << 32) | s->slp_timer_reg[0];
}

static uint32_t esp32s3_rtc_sources_firing(Esp32s3RtcCntlState *s, uint32_t sources)
{
    uint32_t firing = 0;

    if ((sources & RTC_TRIG_GPIO) && s->gpio_wakeup) {
        firing |= RTC_TRIG_GPIO;
    }
    if ((sources & RTC_TRIG_TIMER) &&
        esp32s3_rtc_now_ticks(s) >= esp32s3_rtc_target_ticks(s)) {
        firing |= RTC_TRIG_TIMER;
    }
    return firing;
}

static void esp32s3_rtc_wake(Esp32s3RtcCntlState *s, uint32_t cause)
{
    s->asleep = false;
    timer_del(s->wakeup_timer);
    s->state0_reg = FIELD_DP32(s->state0_reg, RTC_CNTL_STATE0, SLEEP_EN, 0);
    s->state0_reg = FIELD_DP32(s->state0_reg, RTC_CNTL_STATE0, SLP_WAKEUP, 1);
    s->int_raw_reg = FIELD_DP32(s->int_raw_reg, RTC_CNTL_INT_RAW, SLP_WAKEUP, 1);
    s->slp_wakeup_cause = cause;
}

static uint32_t esp32s3_rtc_wake_sources(Esp32s3RtcCntlState *s)
{
    return FIELD_EX32(s->wakeup_state_reg, RTC_CNTL_WAKEUP_STATE, WAKEUP_ENA);
}

static void esp32s3_rtc_sleep_begin(Esp32s3RtcCntlState *s)
{
    uint32_t wake = esp32s3_rtc_wake_sources(s);
    uint32_t reject = FIELD_EX32(s->slp_reject_conf_reg, RTC_CNTL_SLP_REJECT_CONF,
                                 SLEEP_REJECT_ENA);
    uint32_t firing = esp32s3_rtc_sources_firing(s, wake | reject);

    s->state0_reg = FIELD_DP32(s->state0_reg, RTC_CNTL_STATE0, SLP_REJECT, 0);
    s->state0_reg = FIELD_DP32(s->state0_reg, RTC_CNTL_STATE0, SLP_WAKEUP, 0);
    if (firing & reject) {
        /* A source the firmware asked to be refused by is already firing. */
        s->state0_reg = FIELD_DP32(s->state0_reg, RTC_CNTL_STATE0, SLEEP_EN, 0);
        s->state0_reg = FIELD_DP32(s->state0_reg, RTC_CNTL_STATE0, SLP_REJECT, 1);
        s->int_raw_reg = FIELD_DP32(s->int_raw_reg, RTC_CNTL_INT_RAW, SLP_REJECT, 1);
        s->slp_reject_cause = firing & reject;
        return;
    }
    s->asleep = true;
    if (firing & wake) {
        esp32s3_rtc_wake(s, firing & wake);
        return;
    }
    if (wake & RTC_TRIG_TIMER) {
        /* The moment the RTC time reaches the target, at the current rate. */
        uint64_t ticks = esp32s3_rtc_target_ticks(s);
        int64_t at = s->time_base_ns + (int64_t)muldiv64(ticks, NANOSECONDS_PER_SECOND,
                                                         s->rtc_slowclk_freq);
        timer_mod_ns(s->wakeup_timer, at);
    }
}

static void esp32s3_rtc_wakeup_timer_cb(void *opaque)
{
    Esp32s3RtcCntlState *s = opaque;

    if (s->asleep && (esp32s3_rtc_wake_sources(s) & RTC_TRIG_TIMER)) {
        esp32s3_rtc_wake(s, RTC_TRIG_TIMER);
    }
}

static void esp32s3_rtc_gpio_wakeup(void *opaque, int n, int level)
{
    Esp32s3RtcCntlState *s = opaque;

    s->gpio_wakeup = level != 0;
    if (s->asleep && s->gpio_wakeup &&
        (esp32s3_rtc_wake_sources(s) & RTC_TRIG_GPIO)) {
        esp32s3_rtc_wake(s, RTC_TRIG_GPIO);
    }
}

static void esp32s3_rtc_update_cpu_stall(Esp32s3RtcCntlState* s)
{
    uint32_t procpu_stall = (FIELD_EX32(s->sw_cpu_stall_reg, RTC_CNTL_SW_CPU_STALL, PROCPU_C1) << 2) |
                            (FIELD_EX32(s->options0_reg, RTC_CNTL_OPTIONS0, SW_STALL_PROCPU_C0));

    uint32_t appcpu_stall = (FIELD_EX32(s->sw_cpu_stall_reg, RTC_CNTL_SW_CPU_STALL, APPCPU_C1) << 2) |
                            (FIELD_EX32(s->options0_reg, RTC_CNTL_OPTIONS0, SW_STALL_APPCPU_C0));

    const uint32_t stall_magic_val = 0x86;

    s->cpu_stall_state[0] = procpu_stall == stall_magic_val;
    s->cpu_stall_state[1] = appcpu_stall == stall_magic_val;

    qemu_set_irq(s->cpu_stall_req[0], s->cpu_stall_state[0]);
    qemu_set_irq(s->cpu_stall_req[1], s->cpu_stall_state[1]);
}

static void esp32s3_rtc_update_clk(Esp32s3RtcCntlState* s)
{
    const uint32_t slowclk_freq[] = {150000, 32768, 8000000/256};
    const uint32_t fastclk_freq[] = {s->xtal_apb_freq / 4, 8000000};
    s->rtc_slowclk_freq = slowclk_freq[s->rtc_slowclk];
    s->rtc_fastclk_freq = fastclk_freq[s->rtc_fastclk];
    qemu_irq_pulse(s->clk_update);
}

static const MemoryRegionOps esp32s3_rtc_cntl_ops = {
    .read =  esp32s3_rtc_cntl_read,
    .write = esp32s3_rtc_cntl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_rtc_cntl_reset_hold(Object *obj, ResetType type)
{
    Esp32s3RtcCntlState *s = ESP32S3_RTC_CNTL(obj);

    s->time_base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* A reset ends a light sleep. */
    s->asleep = false;
    s->state0_reg = FIELD_DP32(s->state0_reg, RTC_CNTL_STATE0, SLEEP_EN, 0);
    if (s->wakeup_timer) {
        timer_del(s->wakeup_timer);
    }
}

static void esp32s3_rtc_cntl_realize(DeviceState *dev, Error **errp)
{
}

static void esp32s3_rtc_cntl_init(Object *obj)
{
    Esp32s3RtcCntlState *s = ESP32S3_RTC_CNTL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_rtc_cntl_ops, s,
                          TYPE_ESP32S3_RTC_CNTL, ESP32S3_RTC_CNTL_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->dig_reset_req, ESP32S3_RTC_DIG_RESET_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->cpu_reset_req[0], ESP32S3_RTC_CPU_RESET_GPIO, ESP32S3_CPU_COUNT);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->cpu_stall_req[0], ESP32S3_RTC_CPU_STALL_GPIO, ESP32S3_CPU_COUNT);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->clk_update, ESP32S3_RTC_CLK_UPDATE_GPIO, 1);
    qdev_init_gpio_in_named(DEVICE(sbd), esp32s3_rtc_gpio_wakeup,
                            ESP32S3_RTC_GPIO_WAKEUP_GPIO, 1);
    s->wakeup_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, esp32s3_rtc_wakeup_timer_cb, s);

    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
        s->reset_cause[i] = ESP32_POWERON_RESET;
        s->stat_vector_sel[i] = true;
    }

    s->rtc_slowclk = ESP32_SLOW_CLK_RC;
    s->rtc_fastclk = ESP32_FAST_CLK_8M;
    s->soc_clk = ESP32_SOC_CLK_XTAL;
    s->xtal_apb_freq = 40000000;
    s->pll_apb_freq = 80000000;
    esp32s3_rtc_update_clk(s);
}

static Property esp32s3_rtc_cntl_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_rtc_cntl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32s3_rtc_cntl_reset_hold;
    dc->realize = esp32s3_rtc_cntl_realize;
    device_class_set_props(dc, esp32s3_rtc_cntl_properties);
}

static const TypeInfo esp32s3_rtc_cntl_info = {
    .name = TYPE_ESP32S3_RTC_CNTL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3RtcCntlState),
    .instance_init = esp32s3_rtc_cntl_init,
    .class_init = esp32s3_rtc_cntl_class_init
};

static void esp32s3_rtc_cntl_register_types(void)
{
    type_register_static(&esp32s3_rtc_cntl_info);
}

type_init(esp32s3_rtc_cntl_register_types)

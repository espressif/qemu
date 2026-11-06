/*
 * Bare-metal ESP32-C3 SAR ADC test for the QEMU model.
 *
 * Drives APB_SARADC with the register sequences ESP-IDF's oneshot HAL uses,
 * including its habit of updating one bitfield per read-modify-write. See
 * README.md for what is covered, and ../esp32c3-i2c/README.md for why this
 * is a guest rather than a qtest.
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define UART0        0x60000000u
#define SARADC       0x60040000u
#define A_CTRL       0x00
#define A_ONETIME    0x20
#define A_DATA1      0x2c
#define A_DATA2      0x30
#define A_INT_ENA    0x40
#define A_INT_RAW    0x44
#define A_INT_ST     0x48
#define A_INT_CLR    0x4c

/* APB_SARADC.onetime_sample bitfields, as (shift, width) */
#define F_ATTEN      23, 2
#define F_CHANNEL    25, 4
#define F_START      29, 1
#define F_ADC2       30, 1
#define F_ADC1       31, 1

#define ADC1_DONE    (1u << 31)
#define ADC2_DONE    (1u << 30)

/* What run.sh passes as adc-pad<n>; pads it leaves out read the default */
#ifndef EXPECT_PAD0
#define EXPECT_PAD0 0
#endif
#ifndef EXPECT_PAD4
#define EXPECT_PAD4 4095
#endif
#ifndef EXPECT_PAD5
#define EXPECT_PAD5 1234
#endif
#define EXPECT_DEFAULT 2048

static void putc_(char c) { REG(UART0) = (uint32_t)(unsigned char)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex8(uint32_t v)
{
    const char *d = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        putc_(d[(v >> shift) & 0xf]);
    }
}
static void dec(uint32_t v)
{
    char buf[10];
    int n = 0;
    do {
        buf[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (n) {
        putc_(buf[--n]);
    }
}

static int fails;

static void verdict(int ok)
{
    puts_(ok ? " ok\r\n" : " FAIL\r\n");
    fails += !ok;
}

/*
 * One bitfield assignment the way the compiler emits it for the HAL's
 * `APB_SARADC.onetime_sample.x = v`: read the word, change the field, write
 * the word back. Every other field goes through the device and back.
 */
static void field(uint32_t off, uint32_t shift, uint32_t width, uint32_t v)
{
    const uint32_t mask = ((1u << width) - 1) << shift;
    REG(SARADC + off) = (REG(SARADC + off) & ~mask) | ((v << shift) & mask);
}

static void clear_event(uint32_t mask)
{
    /* adc_oneshot_ll_clear_event(): `int_clr.val |= mask` */
    REG(SARADC + A_INT_CLR) = REG(SARADC + A_INT_CLR) | mask;
}

static void select_unit(int unit)
{
    /* adc_oneshot_ll_disable_all_unit() then adc_oneshot_ll_enable() */
    field(A_ONETIME, F_ADC1, 0);
    field(A_ONETIME, F_ADC2, 0);
    if (unit == 0) {
        field(A_ONETIME, F_ADC1, 1);
    } else {
        field(A_ONETIME, F_ADC2, 1);
    }
}

static void start(void)
{
    /* adc_oneshot_ll_start(false); delay; adc_oneshot_ll_start(true) */
    field(A_ONETIME, F_START, 0);
    field(A_ONETIME, F_START, 1);
}

/*
 * adc_oneshot_hal_setup() + adc_oneshot_hal_convert(). Returns the data
 * status word, and the number of INT_RAW reads it took to see DONE in *polls
 * (0 if it never came). The driver has no limit there; this does, so that a
 * broken model fails the test instead of hanging it.
 */
static uint32_t convert(int unit, int channel, uint32_t *polls)
{
    const uint32_t event = unit == 0 ? ADC1_DONE : ADC2_DONE;

    field(A_ONETIME, F_CHANNEL, (unit << 3) | channel);
    field(A_ONETIME, F_ATTEN, 3);

    clear_event(event);
    select_unit(unit);
    start();

    *polls = 0;
    for (uint32_t i = 1; i <= 1000; i++) {
        if (REG(SARADC + A_INT_RAW) & event) {
            *polls = i;
            break;
        }
    }
    return REG(SARADC + (unit == 0 ? A_DATA1 : A_DATA2));
}

/*
 * adc_oneshot_ll_raw_check_valid(): an ADC2 result is thrown away when the
 * channel bits above the code read more than 9. ADC1 results are not checked.
 */
static int valid(int unit, uint32_t data)
{
    return unit == 0 || ((data >> 13) & 0xf) <= 9;
}

static void check_readback(void)
{
    uint32_t got;
    const uint32_t want = (2u << 23) | (5u << 25) | (1u << 31);

    REG(SARADC + A_ONETIME) = 0;
    field(A_ONETIME, F_ATTEN, 2);
    field(A_ONETIME, F_CHANNEL, 5);
    field(A_ONETIME, F_ADC1, 1);
    got = REG(SARADC + A_ONETIME);
    puts_("readback ONETIME_SAMPLE=0x"); hex8(got);
    verdict(got == want);

    /* Plain storage elsewhere too: the HAL configures CTRL the same way */
    REG(SARADC + A_CTRL) = 0;
    field(A_CTRL, 7, 8, 0x5a);
    field(A_CTRL, 15, 4, 0x9);
    got = REG(SARADC + A_CTRL);
    puts_("readback CTRL=0x"); hex8(got);
    verdict(got == ((0x5au << 7) | (0x9u << 15)));
    REG(SARADC + A_CTRL) = 0;
    REG(SARADC + A_ONETIME) = 0;
}

/*
 * cal_setup() + read_cal_channel() from adc_hal_common.c: the conversion the
 * boot-time self-calibration repeats ~140 times per unit and attenuation,
 * each followed by `while (!adc_oneshot_ll_get_event(event));`.
 */
static void check_cal(int unit)
{
    const uint32_t event = unit == 0 ? ADC1_DONE : ADC2_DONE;
    /* adc_oneshot_ll_disable_channel(): 0xf for ADC1, 0x1 for ADC2 */
    const uint32_t channel = (unit << 3) | (unit == 0 ? 0xf : 0x1);
    uint32_t raw_first, data;

    field(A_ONETIME, F_ADC1, 0);
    field(A_ONETIME, F_ADC2, 0);
    field(A_ONETIME, F_CHANNEL, channel);
    field(A_ONETIME, F_ATTEN, 0);
    field(A_ONETIME, unit == 0 ? 31 : 30, 1, 1);

    /*
     * The driver clears only its own event. Clear the other unit's as well,
     * so that the check below can tell a DONE on the wrong unit from one a
     * previous conversion left behind.
     */
    clear_event(ADC1_DONE | ADC2_DONE);
    start();
    /* The very first read has to see DONE: there is nothing to wait for */
    raw_first = REG(SARADC + A_INT_RAW);
    data = REG(SARADC + (unit == 0 ? A_DATA1 : A_DATA2));

    puts_("cal ADC"); dec(unit + 1);
    puts_(" INT_RAW=0x"); hex8(raw_first);
    puts_(" data=0x"); hex8(data);
    /* Internal ground reads 0, and only this unit's DONE is raised */
    verdict(raw_first == event && (data & 0xfff) == 0 && valid(unit, data));
    clear_event(event);
}

static void check_convert(int unit, int channel, uint32_t expect)
{
    uint32_t polls;
    const uint32_t data = convert(unit, channel, &polls);

    puts_("convert ADC"); dec(unit + 1);
    puts_(" ch"); dec(channel);
    puts_(" polls="); dec(polls);
    puts_(" raw="); dec(data & 0xfff);
    puts_(" expect="); dec(expect);
    verdict(polls == 1 && (data & 0xfff) == expect && valid(unit, data));
}

static void check_events(void)
{
    uint32_t polls, raw, st;

    /* Earlier conversions leave DONE set, as the driver does */
    clear_event(ADC1_DONE | ADC2_DONE);

    convert(0, 2, &polls);
    st = REG(SARADC + A_INT_RAW);
    clear_event(ADC1_DONE);
    raw = REG(SARADC + A_INT_RAW);
    puts_("int_clr before=0x"); hex8(st);
    puts_(" after=0x"); hex8(raw);
    verdict(st == ADC1_DONE && raw == 0);

    /*
     * ONETIME_START is still set here. The driver's next conversion begins
     * by rewriting the unit-enable fields, and must not get a DONE out of
     * that: only start going 0 -> 1 converts.
     */
    select_unit(0);
    field(A_ONETIME, F_CHANNEL, 3);
    raw = REG(SARADC + A_INT_RAW);
    puts_("no-retrigger INT_RAW=0x"); hex8(raw);
    verdict(raw == 0);

    /* No unit selected: a start edge converts nothing */
    field(A_ONETIME, F_ADC1, 0);
    field(A_ONETIME, F_ADC2, 0);
    start();
    raw = REG(SARADC + A_INT_RAW);
    puts_("no-unit INT_RAW=0x"); hex8(raw);
    verdict(raw == 0);

    /* INT_ST is INT_RAW masked by INT_ENA */
    convert(0, 2, &polls);
    st = REG(SARADC + A_INT_ST);
    REG(SARADC + A_INT_ENA) = ADC1_DONE;
    raw = REG(SARADC + A_INT_ST);
    REG(SARADC + A_INT_ENA) = 0;
    puts_("int_st masked=0x"); hex8(st);
    puts_(" enabled=0x"); hex8(raw);
    verdict(st == 0 && raw == ADC1_DONE);

    /* The result registers are read-only */
    raw = REG(SARADC + A_DATA1);
    REG(SARADC + A_DATA1) = 0xffffffffu;
    st = REG(SARADC + A_DATA1);
    puts_("data-ro before=0x"); hex8(raw);
    puts_(" after=0x"); hex8(st);
    verdict(st == raw);
    clear_event(ADC1_DONE | ADC2_DONE);
}

void test_main(void)
{
    puts_("ADCTEST START\r\n");

    check_readback();

    check_cal(0);
    check_cal(1);
    /* Back to back, as the binary search does it */
    check_cal(0);

    /* The pads run.sh gave a value to, including both ends of the range */
    check_convert(1, 0, EXPECT_PAD5);
    check_convert(0, 0, EXPECT_PAD0);
    check_convert(0, 4, EXPECT_PAD4);
    /* ...and ones it did not */
    check_convert(0, 2, EXPECT_DEFAULT);
    check_convert(0, 3, EXPECT_DEFAULT);
    /* Calibration left nothing behind that changes a later reading */
    check_cal(1);
    check_convert(1, 0, EXPECT_PAD5);
    /* ADC1 channels 5..7 have no pad on the C3 */
    check_convert(0, 6, 0);

    check_events();

    puts_(fails ? "ADCTEST FAILURES\r\n" : "ADCTEST ALL OK\r\n");
    for (;;) {
    }
}

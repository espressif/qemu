/*
 * QTest for the ESP8266 SoC devices
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"

#define UART0_DATE      0x60000078
#define GPIO_IN         0x60000318
#define RTC_RESET_CAUSE 0x60000714
#define RTC_MEM_USER    0x60001100
#define SAR_SAMPLE0     0x60000d80
#define DPORT_CPU_CLK   0x3ff00014
#define DPORT_REG0_ALIAS 0x3ff00100     /* DPORT wraps every 64 registers */

static QTestState *esp8266_init(void)
{
    return qtest_init("-machine esp8266 -nic none");
}

/* The CPU clock follows DPORT's CPU2X bit: 80 or 160 MHz. */
static uint64_t cpu_clock_hz(QTestState *qts)
{
    QDict *rsp = qtest_qmp(qts, "{'execute': 'qom-get', 'arguments': "
                           "{'path': '/machine/soc/cpu/clk-in', "
                           "'property': 'qtest-clock-period'}}");
    uint64_t period = qdict_get_int(rsp, "return");

    qobject_unref(rsp);
    return (1000000000ULL << 32) / period;
}

static void test_cpu_clock(void)
{
    QTestState *qts = esp8266_init();

    g_assert_cmpuint(cpu_clock_hz(qts), ==, 80000000);
    qtest_writel(qts, DPORT_CPU_CLK, 1);
    g_assert_cmpuint(cpu_clock_hz(qts), ==, 160000000);
    qtest_writel(qts, DPORT_CPU_CLK, 0);
    g_assert_cmpuint(cpu_clock_hz(qts), ==, 80000000);
    g_assert_cmphex(qtest_readl(qts, DPORT_REG0_ALIAS + 0x14), ==, 0);
    qtest_quit(qts);
}

/* Decode a SAR sample the way the SDK's system_adc_read() does. */
static unsigned sar_decode(uint32_t sample)
{
    uint32_t v = ~sample & 0x7ff;
    uint32_t lo = v & 0xff;
    uint32_t corr = MIN(255, (lo > 21 ? lo - 21 : 0) * 279 >> 8);
    uint32_t tout = (8 * ((v & 0xf00) + corr) + 8) >> 4;

    return MIN(1024, tout * 12 / 11);
}

static void test_sar_adc(void)
{
    QTestState *qts = esp8266_init();
    QDict *rsp;

    g_assert_cmpuint(sar_decode(qtest_readl(qts, SAR_SAMPLE0)), <=, 2);
    rsp = qtest_qmp(qts, "{'execute': 'qom-set', 'arguments': "
                    "{'path': '/machine', 'property': 'adc', 'value': 512}}");
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    g_assert_cmpint(abs((int)sar_decode(qtest_readl(qts, SAR_SAMPLE0)) - 512),
                    <=, 2);
    qtest_quit(qts);
}

static void test_rtc(void)
{
    QTestState *qts = esp8266_init();

    g_assert_cmpuint(qtest_readl(qts, RTC_RESET_CAUSE), ==, 1); /* power-on */
    qtest_writel(qts, RTC_MEM_USER, 0x51454d55);
    g_assert_cmphex(qtest_readl(qts, RTC_MEM_USER), ==, 0x51454d55);
    /* RTC memory survives a system reset. */
    qobject_unref(qtest_qmp(qts, "{'execute': 'system_reset'}"));
    qtest_qmp_eventwait(qts, "RESET");
    g_assert_cmphex(qtest_readl(qts, RTC_MEM_USER), ==, 0x51454d55);
    qtest_quit(qts);
}

static void test_reset_values(void)
{
    QTestState *qts = esp8266_init();

    g_assert_cmphex(qtest_readl(qts, UART0_DATE), ==, 0x62000);
    /* Undriven inputs read high; bits 16.. carry the flash boot strapping. */
    g_assert_cmphex(qtest_readl(qts, GPIO_IN) & 0xffff, ==, 0xffff);
    g_assert_cmphex((qtest_readl(qts, GPIO_IN) >> 16) & 0x7, ==, 0x3);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/esp8266/dport/cpu-clock", test_cpu_clock);
    qtest_add_func("/esp8266/sar/adc", test_sar_adc);
    qtest_add_func("/esp8266/rtc", test_rtc);
    qtest_add_func("/esp8266/reset-values", test_reset_values);
    return g_test_run();
}

/*
 * QTest for the ESP32 DPORT interrupt status registers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"

#define DPORT_PRO_INTR_STATUS_0  0x3FF000EC
#define DPORT_APP_INTR_STATUS_0  0x3FF000F8
#define TG1_WDT_LEVEL_SOURCE     20
#define UART0_SOURCE             34
#define CACHE_IA_SOURCE          68

static uint32_t status(QTestState *qts, uint32_t base, int word)
{
    return qtest_readl(qts, base + 4 * word);
}

static void set_source(QTestState *qts, int source, int level)
{
    qtest_set_irq_in(qts, "/machine/soc/intmatrix", NULL, source, level);
}

static void test_status(void)
{
    QTestState *qts = qtest_init("-machine esp32");
    const uint32_t pro = DPORT_PRO_INTR_STATUS_0;
    const uint32_t app = DPORT_APP_INTR_STATUS_0;

    /* No source is asserted after reset */
    for (int word = 0; word < 3; ++word) {
        g_assert_cmphex(status(qts, pro, word), ==, 0);
        g_assert_cmphex(status(qts, app, word), ==, 0);
    }

    /* Both CPUs see the raw level of a source, whatever its mapping */
    set_source(qts, TG1_WDT_LEVEL_SOURCE, 1);
    set_source(qts, UART0_SOURCE, 1);
    set_source(qts, CACHE_IA_SOURCE, 1);
    g_assert_cmphex(status(qts, pro, 0), ==, BIT(TG1_WDT_LEVEL_SOURCE));
    g_assert_cmphex(status(qts, app, 0), ==, BIT(TG1_WDT_LEVEL_SOURCE));
    g_assert_cmphex(status(qts, pro, 1), ==, BIT(UART0_SOURCE - 32));
    g_assert_cmphex(status(qts, app, 2), ==, BIT(CACHE_IA_SOURCE - 64));

    /* The registers are read-only, and a lowered source clears its bit */
    qtest_writel(qts, pro, 0);
    g_assert_cmphex(status(qts, pro, 0), ==, BIT(TG1_WDT_LEVEL_SOURCE));
    set_source(qts, TG1_WDT_LEVEL_SOURCE, 0);
    g_assert_cmphex(status(qts, pro, 0), ==, 0);
    g_assert_cmphex(status(qts, app, 0), ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/esp32/intmatrix/status", test_status);
    return g_test_run();
}

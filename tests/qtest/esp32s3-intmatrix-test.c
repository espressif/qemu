/*
 * QTest for the ESP32-S3 interrupt matrix INTR_STATUS registers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"

#define INTMATRIX_CORE0_STATUS  0x600C218C
#define INTMATRIX_CORE1_STATUS  0x600C298C
#define UART0_SOURCE            27
#define SHA_SOURCE              81
#define TG1_WDT_LEVEL_SOURCE    55

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
    QTestState *qts = qtest_init("-machine esp32s3");
    const uint32_t core0 = INTMATRIX_CORE0_STATUS;
    const uint32_t core1 = INTMATRIX_CORE1_STATUS;

    /* No source is asserted after reset */
    for (int word = 0; word < 4; ++word) {
        g_assert_cmphex(status(qts, core0, word), ==, 0);
        g_assert_cmphex(status(qts, core1, word), ==, 0);
    }

    /* Both CPUs see the raw level of a source, whatever its mapping */
    set_source(qts, UART0_SOURCE, 1);
    set_source(qts, SHA_SOURCE, 1);
    g_assert_cmphex(status(qts, core0, 0), ==, BIT(UART0_SOURCE));
    g_assert_cmphex(status(qts, core1, 0), ==, BIT(UART0_SOURCE));
    g_assert_cmphex(status(qts, core0, 2), ==, BIT(SHA_SOURCE - 64));

    /* The registers are read-only, and a lowered source clears its bit */
    qtest_writel(qts, core0, 0);
    g_assert_cmphex(status(qts, core0, 0), ==, BIT(UART0_SOURCE));
    set_source(qts, UART0_SOURCE, 0);
    g_assert_cmphex(status(qts, core0, 0), ==, 0);

    qtest_quit(qts);
}

static void test_system_reset(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    /* A system reset deasserts every source, also those without a reset */
    set_source(qts, TG1_WDT_LEVEL_SOURCE, 1);
    g_assert_cmphex(status(qts, INTMATRIX_CORE0_STATUS, 1), ==,
                    BIT(TG1_WDT_LEVEL_SOURCE - 32));
    qtest_qmp_assert_success(qts, "{'execute': 'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    g_assert_cmphex(status(qts, INTMATRIX_CORE0_STATUS, 1), ==, 0);
    g_assert_cmphex(status(qts, INTMATRIX_CORE1_STATUS, 1), ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/esp32s3/intmatrix/status", test_status);
    qtest_add_func("/esp32s3/intmatrix/system_reset", test_system_reset);
    return g_test_run();
}

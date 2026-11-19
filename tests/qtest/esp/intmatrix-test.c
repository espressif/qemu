/*
 * QTest for the ESP32 and ESP32-S3 interrupt matrix INTR_STATUS registers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "esp-qtest.h"

typedef struct {
    const char *machine;
    uint32_t core0_status;
    uint32_t core1_status;
    int status_words;
    int word0_source;
    int word1_source;
    int word2_source;
    int reset_source;
} IntMatrixCase;

static uint32_t read_status(QTestState *qts, uint32_t base, int word)
{
    return esp_reg_readl(qts, base, 4 * word);
}

static void test_status(const void *data)
{
    const IntMatrixCase *c = data;
    QTestState *qts = qtest_initf("-machine %s", c->machine);

    /* No source is asserted after reset */
    for (int word = 0; word < c->status_words; ++word) {
        g_assert_cmphex(read_status(qts, c->core0_status, word), ==, 0);
        g_assert_cmphex(read_status(qts, c->core1_status, word), ==, 0);
    }

    /* Both CPUs see the raw level of a source, whatever its mapping */
    esp_qtest_set_irq(qts, c->word0_source, 1);
    esp_qtest_set_irq(qts, c->word1_source, 1);
    esp_qtest_set_irq(qts, c->word2_source, 1);
    g_assert_cmphex(read_status(qts, c->core0_status, 0), ==,
                    BIT(c->word0_source));
    g_assert_cmphex(read_status(qts, c->core1_status, 0), ==,
                    BIT(c->word0_source));
    g_assert_cmphex(read_status(qts, c->core0_status, 1), ==,
                    BIT(c->word1_source - 32));
    g_assert_cmphex(read_status(qts, c->core1_status, 2), ==,
                    BIT(c->word2_source - 64));

    /* The registers are read-only, and a lowered source clears its bit */
    qtest_writel(qts, c->core0_status, 0);
    g_assert_cmphex(read_status(qts, c->core0_status, 0), ==,
                    BIT(c->word0_source));
    esp_qtest_set_irq(qts, c->word0_source, 0);
    g_assert_cmphex(read_status(qts, c->core0_status, 0), ==, 0);
    g_assert_cmphex(read_status(qts, c->core1_status, 0), ==, 0);

    qtest_quit(qts);
}

static void test_system_reset(const void *data)
{
    const IntMatrixCase *c = data;
    QTestState *qts = qtest_initf("-machine %s", c->machine);
    int word = c->reset_source / 32;
    int bit = c->reset_source % 32;

    /* A system reset deasserts every source, also those without a reset */
    esp_qtest_set_irq(qts, c->reset_source, 1);
    g_assert_cmphex(read_status(qts, c->core0_status, word), ==, BIT(bit));
    esp_qtest_system_reset(qts);
    g_assert_cmphex(read_status(qts, c->core0_status, word), ==, 0);
    g_assert_cmphex(read_status(qts, c->core1_status, word), ==, 0);

    qtest_quit(qts);
}

static void register_intmatrix_tests(void)
{
    static const IntMatrixCase cases[] = {
        {
            .machine = "esp32",
            .core0_status = 0x3FF000EC,
            .core1_status = 0x3FF000F8,
            .status_words = 3,
            .word0_source = 20, /* TG1_WDT_LEVEL */
            .word1_source = 34, /* UART0 */
            .word2_source = 68, /* CACHE_IA */
            .reset_source = 20,
        },
        {
            .machine = "esp32s3",
            .core0_status = 0x600C218C,
            .core1_status = 0x600C298C,
            .status_words = 4,
            .word0_source = 27, /* UART0 */
            .word1_source = 55, /* TG1_WDT_LEVEL */
            .word2_source = 81, /* SHA */
            .reset_source = 55,
        },
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        if (!qtest_has_machine(cases[i].machine)) {
            continue;
        }
        g_autofree char *status_path =
            g_strdup_printf("/esp/intmatrix/%s/status", cases[i].machine);
        g_autofree char *reset_path =
            g_strdup_printf("/esp/intmatrix/%s/system-reset", cases[i].machine);
        qtest_add_data_func(status_path, &cases[i], test_status);
        qtest_add_data_func(reset_path, &cases[i], test_system_reset);
    }
}

ESP_QTEST_SUITE(register_intmatrix_tests)

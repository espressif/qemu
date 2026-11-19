/*
 * QTest for the ESP timer group registers.
 * The expected values were measured on silicon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/misc/esp32s3_reg.h"
#include "hw/timer/esp_timg.h"
#include "esp-qtest.h"

#define TIMG_RESET_CONFIG 0x60002000

typedef struct {
    const char *machine;
    uint32_t timg_base;
} TimgCase;

static uint64_t timg_read(QTestState *qts, uint32_t base)
{
    esp_reg_writel(qts, base, A_TIMG_T0UPDATE, R_TIMG_T0UPDATE_UPDATE_MASK);
    return ((uint64_t)esp_reg_readl(qts, base, A_TIMG_T0HI) << 32) |
           esp_reg_readl(qts, base, A_TIMG_T0LO);
}

static void timg_start(QTestState *qts, uint32_t base, uint32_t config)
{
    esp_reg_writel(qts, base, A_TIMG_T0CONFIG, config);
    esp_reg_writel(qts, base, A_TIMG_T0LOADLO, 0);
    esp_reg_writel(qts, base, A_TIMG_T0LOADHI, 0);
    esp_reg_writel(qts, base, A_TIMG_T0LOAD, 1);
    esp_reg_writel(qts, base, A_TIMG_T0CONFIG,
                   config | R_TIMG_T0CONFIG_EN_MASK);
}

static uint64_t timg_ticks_per_second(QTestState *qts, uint32_t base,
                                      uint32_t divider)
{
    uint32_t cfg = R_TIMG_T0CONFIG_INCREASE_MASK;
    cfg = FIELD_DP32(cfg, TIMG_T0CONFIG, DIVIDER, divider);
    timg_start(qts, base, cfg);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    return timg_read(qts, base);
}

static void test_timg(const void *data)
{
    const TimgCase *c = data;
    QTestState *qts = qtest_initf("-machine %s", c->machine);
    uint32_t base = c->timg_base;

    g_assert_cmphex(esp_reg_readl(qts, base, A_TIMG_T0CONFIG), ==,
                    TIMG_RESET_CONFIG);

    g_assert_cmpuint(timg_ticks_per_second(qts, base, 80), ==, 1000000);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 3), ==, 26666666);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 2), ==, 40000000);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 1), ==, 40000000);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 0), ==, 1220);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 65535), ==, 1220);

    /* DIVCNT_RST leaves the rate and the count alone */
    uint32_t cfg80 = FIELD_DP32(R_TIMG_T0CONFIG_INCREASE_MASK,
                                TIMG_T0CONFIG, DIVIDER, 80);
    timg_start(qts, base, cfg80);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    esp_reg_writel(qts, base, A_TIMG_T0CONFIG,
                   cfg80 | R_TIMG_T0CONFIG_EN_MASK |
                   R_TIMG_T0CONFIG_DIVCNT_RST_MASK);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    g_assert_cmpuint(timg_read(qts, base), ==, 2000000);

    /* Reading faster than the counter ticks does not stop it */
    timg_start(qts, base, R_TIMG_T0CONFIG_INCREASE_MASK);
    for (int i = 0; i < 1000; i++) {
        qtest_clock_step(qts, NANOSECONDS_PER_SECOND / 1000);
        timg_read(qts, base);
    }
    g_assert_cmpuint(timg_read(qts, base), ==, 1220);

    qtest_quit(qts);
}

static void register_timg_tests(void)
{
    static const TimgCase esp32s3 = { "esp32s3", DR_REG_TIMERGROUP0_BASE };

    if (qtest_has_machine(esp32s3.machine)) {
        qtest_add_data_func("/esp/timg/esp32s3", &esp32s3, test_timg);
    }
}

ESP_QTEST_SUITE(register_timg_tests)

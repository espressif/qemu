/*
 * QTest for the ESP32-S3 timer groups, GDMA and Bluetooth clock divider
 * registers. The expected values were measured on silicon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"

#define TIMG_T0CONFIG        0x00
#define TIMG_T0LO            0x04
#define TIMG_T0HI            0x08
#define TIMG_T0UPDATE        0x0c
#define TIMG_T0LOADLO        0x18
#define TIMG_T0LOADHI        0x1c
#define TIMG_T0LOAD          0x20

#define TIMG_EN              BIT(31)
#define TIMG_INCREASE        BIT(30)
#define TIMG_DIVCNT_RST      BIT(12)
#define TIMG_DIVIDER(n)      ((uint32_t)(n) << 13)
#define TIMG_RESET_CONFIG    0x60002000

#define S3_SYSTEM_BASE       0x600c0000
#define S3_BT_LPCK_DIV_INT   (S3_SYSTEM_BASE + 0x28)
#define S3_BT_LPCK_DIV_FRAC  (S3_SYSTEM_BASE + 0x2c)

#define S3_MMU_TABLE         0x600c5000
#define S3_MMU_PSRAM         BIT(15)
#define S3_DCACHE            0x3c000000
#define S3_DRAM              0x3fc90000
#define S3_IRAM              0x40378000

#define S3_GDMA_BASE         0x6003f000
#define S3_IN_CONF0          (S3_GDMA_BASE + 0x00)
#define S3_IN_INT_RAW        (S3_GDMA_BASE + 0x08)
#define S3_IN_INT_CLR        (S3_GDMA_BASE + 0x14)
#define S3_IN_LINK           (S3_GDMA_BASE + 0x20)
#define S3_OUT_CONF0         (S3_GDMA_BASE + 0x60)
#define S3_OUT_INT_RAW       (S3_GDMA_BASE + 0x68)
#define S3_OUT_INT_CLR       (S3_GDMA_BASE + 0x74)
#define S3_OUT_LINK          (S3_GDMA_BASE + 0x80)
#define S3_OUT_EOF_DES_ADDR  (S3_GDMA_BASE + 0x88)

#define MEM_TRANS_EN         BIT(4)
#define IN_LINK_START        BIT(22)
#define OUT_LINK_START       BIT(21)
#define IN_SUC_EOF           BIT(1)
#define IN_DSCR_ERR          BIT(3)
#define OUT_EOF              BIT(1)
#define OUT_DSCR_ERR         BIT(2)
#define OUT_TOTAL_EOF        BIT(3)

#define DESC_OWNER           BIT(31)
#define DESC_EOF             BIT(30)
#define DESC_CFG(len, eof)   (DESC_OWNER | ((eof) ? DESC_EOF : 0) | ((len) << 12) | (len))

typedef struct {
    const char *machine;
    uint32_t timg_base;
} TimgCase;

static uint64_t timg_read(QTestState *qts, uint32_t base)
{
    qtest_writel(qts, base + TIMG_T0UPDATE, BIT(31));
    return ((uint64_t)qtest_readl(qts, base + TIMG_T0HI) << 32) |
           qtest_readl(qts, base + TIMG_T0LO);
}

static void timg_start(QTestState *qts, uint32_t base, uint32_t config)
{
    qtest_writel(qts, base + TIMG_T0CONFIG, config);
    qtest_writel(qts, base + TIMG_T0LOADLO, 0);
    qtest_writel(qts, base + TIMG_T0LOADHI, 0);
    qtest_writel(qts, base + TIMG_T0LOAD, 1);
    qtest_writel(qts, base + TIMG_T0CONFIG, config | TIMG_EN);
}

static uint64_t timg_ticks_per_second(QTestState *qts, uint32_t base, uint32_t divider)
{
    timg_start(qts, base, TIMG_INCREASE | TIMG_DIVIDER(divider));
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    return timg_read(qts, base);
}

static void test_timg(const void *data)
{
    const TimgCase *c = data;
    g_autofree char *args = g_strdup_printf("-machine %s", c->machine);
    QTestState *qts = qtest_init(args);
    uint32_t base = c->timg_base;

    g_assert_cmphex(qtest_readl(qts, base + TIMG_T0CONFIG), ==, TIMG_RESET_CONFIG);

    g_assert_cmpuint(timg_ticks_per_second(qts, base, 80), ==, 1000000);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 3), ==, 26666666);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 2), ==, 40000000);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 1), ==, 40000000);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 0), ==, 1220);
    g_assert_cmpuint(timg_ticks_per_second(qts, base, 65535), ==, 1220);

    /* DIVCNT_RST leaves the rate and the count alone */
    timg_start(qts, base, TIMG_INCREASE | TIMG_DIVIDER(80));
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    qtest_writel(qts, base + TIMG_T0CONFIG,
                 TIMG_EN | TIMG_INCREASE | TIMG_DIVIDER(80) | TIMG_DIVCNT_RST);
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);
    g_assert_cmpuint(timg_read(qts, base), ==, 2000000);

    /* Reading faster than the counter ticks does not stop it */
    timg_start(qts, base, TIMG_INCREASE);
    for (int i = 0; i < 1000; i++) {
        qtest_clock_step(qts, NANOSECONDS_PER_SECOND / 1000);
        timg_read(qts, base);
    }
    g_assert_cmpuint(timg_read(qts, base), ==, 1220);

    qtest_quit(qts);
}

static void s3_map_psram(QTestState *qts)
{
    qtest_writel(qts, S3_MMU_TABLE, S3_MMU_PSRAM);
}

static void s3_m2m(QTestState *qts, uint32_t out_desc, uint32_t in_desc)
{
    qtest_writel(qts, S3_IN_INT_CLR, UINT32_MAX);
    qtest_writel(qts, S3_OUT_INT_CLR, UINT32_MAX);
    qtest_writel(qts, S3_IN_CONF0, MEM_TRANS_EN);
    qtest_writel(qts, S3_OUT_LINK, (out_desc & 0xfffff) | OUT_LINK_START);
    qtest_writel(qts, S3_IN_LINK, (in_desc & 0xfffff) | IN_LINK_START);
}

static void s3_desc(QTestState *qts, uint32_t addr, uint32_t cfg, uint32_t buf, uint32_t next)
{
    qtest_writel(qts, addr, cfg);
    qtest_writel(qts, addr + 4, buf);
    qtest_writel(qts, addr + 8, next);
}

static void test_s3_gdma_eof(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");
    const uint32_t out = S3_DRAM, in = S3_DRAM + 0x40;
    const uint32_t src = S3_DRAM + 0x100, dst = S3_DRAM + 0x200;

    qtest_writel(qts, src, 0x11111111);
    qtest_writel(qts, src + 4, 0x22222222);
    s3_desc(qts, out, DESC_CFG(4, false), src, out + 0x10);
    s3_desc(qts, out + 0x10, DESC_CFG(4, true), src + 4, 0);
    s3_desc(qts, in, DESC_OWNER | 8, dst, 0);
    s3_m2m(qts, out, in);

    g_assert_cmphex(qtest_readl(qts, dst + 4), ==, 0x22222222);
    g_assert_cmphex(qtest_readl(qts, S3_OUT_EOF_DES_ADDR), ==, out + 0x10);
    g_assert_cmphex(qtest_readl(qts, S3_OUT_INT_RAW) & (OUT_EOF | OUT_TOTAL_EOF), ==,
                    OUT_EOF | OUT_TOTAL_EOF);

    qtest_quit(qts);
}

static void test_s3_gdma_psram(void)
{
    QTestState *qts = qtest_init("-machine esp32s3 -m 4M");
    const uint32_t out = S3_DRAM, in = S3_DRAM + 0x40;
    const uint32_t src = S3_DRAM + 0x100, dst = S3_DRAM + 0x200;

    s3_map_psram(qts);

    /* Internal RAM to PSRAM */
    qtest_writel(qts, src, 0x12345678);
    s3_desc(qts, out, DESC_CFG(4, true), src, 0);
    s3_desc(qts, in, DESC_OWNER | 4, S3_DCACHE, 0);
    s3_m2m(qts, out, in);
    g_assert_cmphex(qtest_readl(qts, S3_IN_INT_RAW) & IN_SUC_EOF, ==, IN_SUC_EOF);
    g_assert_cmphex(qtest_readl(qts, S3_DCACHE), ==, 0x12345678);

    /* PSRAM to internal RAM */
    s3_desc(qts, out, DESC_CFG(4, true), S3_DCACHE, 0);
    s3_desc(qts, in, DESC_OWNER | 4, dst, 0);
    s3_m2m(qts, out, in);
    g_assert_cmphex(qtest_readl(qts, S3_IN_INT_RAW) & IN_SUC_EOF, ==, IN_SUC_EOF);
    g_assert_cmphex(qtest_readl(qts, dst), ==, 0x12345678);

    qtest_quit(qts);
}

static void test_s3_gdma_reach(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");
    const uint32_t out = S3_DRAM, in = S3_DRAM + 0x40, dst = S3_DRAM + 0x200;

    /* IRAM is not reachable: nothing is written and the IN descriptor stays with the DMA */
    qtest_writel(qts, dst, 0xeeeeeeee);
    s3_desc(qts, out, DESC_CFG(4, true), S3_IRAM, 0);
    s3_desc(qts, in, DESC_OWNER | 4, dst, 0);
    s3_m2m(qts, out, in);
    g_assert_cmphex(qtest_readl(qts, S3_OUT_INT_RAW) & OUT_DSCR_ERR, ==, OUT_DSCR_ERR);
    g_assert_cmphex(qtest_readl(qts, dst), ==, 0xeeeeeeee);
    g_assert_cmphex(qtest_readl(qts, in) & DESC_OWNER, ==, DESC_OWNER);

    qtest_quit(qts);
}

static void test_s3_bt_lpck(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");

    g_assert_cmphex(qtest_readl(qts, S3_BT_LPCK_DIV_INT), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, S3_BT_LPCK_DIV_FRAC), ==, 0x02001001);

    qtest_writel(qts, S3_BT_LPCK_DIV_INT, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, S3_BT_LPCK_DIV_INT), ==, 0xfff);
    qtest_writel(qts, S3_BT_LPCK_DIV_FRAC, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, S3_BT_LPCK_DIV_FRAC), ==, 0x1fffffff);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    static const TimgCase esp32s3 = { "esp32s3", 0x6001f000 };

    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("esp-periph/timg/esp32s3", &esp32s3, test_timg);
    qtest_add_func("esp-periph/s3/gdma-eof", test_s3_gdma_eof);
    qtest_add_func("esp-periph/s3/gdma-psram", test_s3_gdma_psram);
    qtest_add_func("esp-periph/s3/gdma-reach", test_s3_gdma_reach);
    qtest_add_func("esp-periph/s3/bt-lpck", test_s3_bt_lpck);

    return g_test_run();
}

/*
 * QTest for the ESP32-S3 GDMA controller.
 * The expected values were measured on silicon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "hw/dma/esp32s3_gdma.h"
#include "hw/misc/esp32s3_cache.h"
#include "hw/misc/esp32s3_reg.h"
#include "esp-qtest.h"

#define S3_DRAM              0x3fc90000
#define S3_IRAM              0x40378000

#define S3_GDMA_IN_BASE      DR_REG_GDMA_BASE
#define S3_GDMA_OUT_BASE     (DR_REG_GDMA_BASE + DMA_DIR_REGS_SIZE)

#define DESC_OWNER           BIT(31)
#define DESC_EOF             BIT(30)
#define DESC_CFG(len, eof) \
    (DESC_OWNER | ((eof) ? DESC_EOF : 0) | ((len) << 12) | (len))

static void s3_map_psram(QTestState *qts)
{
    ESP32S3MMUEntry entry = { .type = ESP32S3_MMU_TYPE_PSRAM };
    qtest_writel(qts, DR_REG_MMU_TABLE, entry.val);
}

static void s3_m2m(QTestState *qts, uint32_t out_desc, uint32_t in_desc)
{
    esp_reg_writel(qts, S3_GDMA_IN_BASE, A_DMA_INT_CLR, UINT32_MAX);
    esp_reg_writel(qts, S3_GDMA_OUT_BASE, A_DMA_INT_CLR, UINT32_MAX);
    esp_reg_writel(qts, S3_GDMA_IN_BASE, A_DMA_CONF0,
                   R_GDMA_IN_CONF0_MEM_TRANS_EN_MASK);
    esp_reg_writel(qts, S3_GDMA_OUT_BASE, A_DMA_LINK,
                   (out_desc & R_GDMA_OUT_LINK_ADDR_MASK) |
                   R_GDMA_OUT_LINK_START_MASK);
    esp_reg_writel(qts, S3_GDMA_IN_BASE, A_DMA_LINK,
                   (in_desc & R_GDMA_IN_LINK_ADDR_MASK) |
                   R_GDMA_IN_LINK_START_MASK);
}

static void s3_desc(QTestState *qts, uint32_t addr, uint32_t cfg,
                    uint32_t buf, uint32_t next)
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
    const uint32_t eof_mask = R_DMA_INT_RAW_OUT_EOF_MASK |
                              R_DMA_INT_RAW_OUT_TOTAL_EOF_MASK;

    qtest_writel(qts, src, 0x11111111);
    qtest_writel(qts, src + 4, 0x22222222);
    s3_desc(qts, out, DESC_CFG(4, false), src, out + 0x10);
    s3_desc(qts, out + 0x10, DESC_CFG(4, true), src + 4, 0);
    s3_desc(qts, in, DESC_OWNER | 8, dst, 0);
    s3_m2m(qts, out, in);

    g_assert_cmphex(qtest_readl(qts, dst + 4), ==, 0x22222222);
    g_assert_cmphex(esp_reg_readl(qts, S3_GDMA_OUT_BASE, A_DMA_SUC_EOF_DESC),
                    ==, out + 0x10);
    g_assert_cmphex(esp_reg_readl(qts, S3_GDMA_OUT_BASE, A_DMA_INT_RAW) &
                    eof_mask, ==, eof_mask);

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
    s3_desc(qts, in, DESC_OWNER | 4, ESP32S3_DCACHE_BASE, 0);
    s3_m2m(qts, out, in);
    g_assert_cmphex(esp_reg_readl(qts, S3_GDMA_IN_BASE, A_DMA_INT_RAW) &
                    R_DMA_INT_RAW_IN_SUC_EOF_MASK, ==,
                    R_DMA_INT_RAW_IN_SUC_EOF_MASK);
    g_assert_cmphex(qtest_readl(qts, ESP32S3_DCACHE_BASE), ==, 0x12345678);

    /* PSRAM to internal RAM */
    s3_desc(qts, out, DESC_CFG(4, true), ESP32S3_DCACHE_BASE, 0);
    s3_desc(qts, in, DESC_OWNER | 4, dst, 0);
    s3_m2m(qts, out, in);
    g_assert_cmphex(esp_reg_readl(qts, S3_GDMA_IN_BASE, A_DMA_INT_RAW) &
                    R_DMA_INT_RAW_IN_SUC_EOF_MASK, ==,
                    R_DMA_INT_RAW_IN_SUC_EOF_MASK);
    g_assert_cmphex(qtest_readl(qts, dst), ==, 0x12345678);

    qtest_quit(qts);
}

static void test_s3_gdma_reach(void)
{
    QTestState *qts = qtest_init("-machine esp32s3");
    const uint32_t out = S3_DRAM, in = S3_DRAM + 0x40, dst = S3_DRAM + 0x200;

    /*
     * IRAM is not reachable: nothing is written and the IN descriptor stays
     * with the DMA.
     */
    qtest_writel(qts, dst, 0xeeeeeeee);
    s3_desc(qts, out, DESC_CFG(4, true), S3_IRAM, 0);
    s3_desc(qts, in, DESC_OWNER | 4, dst, 0);
    s3_m2m(qts, out, in);
    g_assert_cmphex(esp_reg_readl(qts, S3_GDMA_OUT_BASE, A_DMA_INT_RAW) &
                    R_DMA_INT_RAW_OUT_DSCR_ERR_MASK, ==,
                    R_DMA_INT_RAW_OUT_DSCR_ERR_MASK);
    g_assert_cmphex(qtest_readl(qts, dst), ==, 0xeeeeeeee);
    g_assert_cmphex(qtest_readl(qts, in) & DESC_OWNER, ==, DESC_OWNER);

    qtest_quit(qts);
}

static void register_gdma_tests(void)
{
    if (qtest_has_machine("esp32s3")) {
        qtest_add_func("/esp/gdma/esp32s3/eof", test_s3_gdma_eof);
        qtest_add_func("/esp/gdma/esp32s3/psram", test_s3_gdma_psram);
        qtest_add_func("/esp/gdma/esp32s3/reach", test_s3_gdma_reach);
    }
}

ESP_QTEST_SUITE(register_gdma_tests)

/*
 * QTest for the ESP32-S3 SPI1 chip selects and the PSRAM model behind them.
 * The expected values were measured on silicon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define SPI1_BASE            0x60002000
#define SPI_CMD              (SPI1_BASE + 0x00)
#define SPI_ADDR             (SPI1_BASE + 0x04)
#define SPI_USER             (SPI1_BASE + 0x18)
#define SPI_USER1            (SPI1_BASE + 0x1c)
#define SPI_USER2            (SPI1_BASE + 0x20)
#define SPI_MOSI_DLEN        (SPI1_BASE + 0x24)
#define SPI_MISO_DLEN        (SPI1_BASE + 0x28)
#define SPI_MISC             (SPI1_BASE + 0x34)
#define SPI_W0               (SPI1_BASE + 0x58)

#define SPI_CMD_USR          BIT(18)
#define SPI_USER_COMMAND     BIT(31)
#define SPI_USER_ADDR        BIT(30)
#define SPI_USER_DUMMY       BIT(29)
#define SPI_USER_MISO        BIT(28)
#define SPI_USER_MOSI        BIT(27)
#define SPI_MISC_CS0_DIS     BIT(0)
#define SPI_MISC_CS1_DIS     BIT(1)

#define PSRAM_OCT_READ       0x0000
#define PSRAM_OCT_WRITE      0x8080
#define PSRAM_SIZE           (8 * 1024 * 1024)

/* A 16-bit command and a 32-bit address, as IDF sends to an octal PSRAM */
static void psram_setup(QTestState *qts, uint32_t cmd, uint32_t addr,
                        uint32_t dummy_cycles, uint32_t user)
{
    qtest_writel(qts, SPI_USER, SPI_USER_COMMAND | SPI_USER_ADDR |
                 SPI_USER_DUMMY | user);
    qtest_writel(qts, SPI_USER1, (31u << 26) | (dummy_cycles - 1));
    qtest_writel(qts, SPI_USER2, (15u << 28) | cmd);
    qtest_writel(qts, SPI_ADDR, addr);
}

static void psram_write(QTestState *qts, uint32_t addr, uint32_t value)
{
    psram_setup(qts, PSRAM_OCT_WRITE, addr, 8, SPI_USER_MOSI);
    qtest_writel(qts, SPI_MOSI_DLEN, 31);
    qtest_writel(qts, SPI_W0, value);
    qtest_writel(qts, SPI_CMD, SPI_CMD_USR);
}

static uint32_t psram_read(QTestState *qts, uint32_t addr)
{
    /* IDF programs 18 cycles for an octal read, OCT_PSRAM_RD_DUMMY_BITLEN */
    psram_setup(qts, PSRAM_OCT_READ, addr, 18, SPI_USER_MISO);
    qtest_writel(qts, SPI_MISO_DLEN, 31);
    qtest_writel(qts, SPI_CMD, SPI_CMD_USR);
    return qtest_readl(qts, SPI_W0);
}

static QTestState *start_octal_psram(void)
{
    return qtest_init("-machine esp32s3 -m 8M "
                      "-global driver=ssi_psram,property=is_octal,value=true");
}

/* SPI_MEM_MISC_REG read 0x2 on two boards, esp32s3-aa20 and esp32s3-9a64 */
static void test_misc_reset(void)
{
    QTestState *qts = start_octal_psram();

    g_assert_cmphex(qtest_readl(qts, SPI_MISC), ==, SPI_MISC_CS1_DIS);
    qtest_quit(qts);
}

/* By default the bus is the flash's, so a transaction must not reach the PSRAM */
static void test_psram_not_selected_by_default(void)
{
    QTestState *qts = start_octal_psram();

    psram_write(qts, 0x10, 0x12345678);
    qtest_writel(qts, SPI_MISC, SPI_MISC_CS0_DIS);
    g_assert_cmphex(psram_read(qts, 0x10), ==, 0);
    qtest_quit(qts);
}

/* An address with bit 31 set used to crash QEMU. esp32s3-aa20 wraps at the size. */
static void test_psram_address_wraps(void)
{
    QTestState *qts = start_octal_psram();

    qtest_writel(qts, SPI_MISC, SPI_MISC_CS0_DIS);

    psram_write(qts, 0x10, 0x11223344);
    g_assert_cmphex(psram_read(qts, 0x10), ==, 0x11223344);
    g_assert_cmphex(psram_read(qts, 0x10 + PSRAM_SIZE), ==, 0x11223344);
    g_assert_cmphex(psram_read(qts, 0x80000010), ==, 0x11223344);

    psram_write(qts, 0xab000020, 0x55667788);
    g_assert_cmphex(psram_read(qts, 0xab000020 % PSRAM_SIZE), ==, 0x55667788);

    /* A burst across the end of the part continues at its start */
    psram_write(qts, PSRAM_SIZE - 2, 0xa1b2c3d4);
    g_assert_cmphex(psram_read(qts, 0) & 0xffff, ==, 0xa1b2);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/esp32s3/spi1/misc_reset", test_misc_reset);
    qtest_add_func("/esp32s3/spi1/psram_not_selected_by_default",
                   test_psram_not_selected_by_default);
    qtest_add_func("/esp32s3/psram/address_wraps", test_psram_address_wraps);

    return g_test_run();
}

/*
 * QTest for the ESP32-S3 SPI1 chip selects and the PSRAM model behind them.
 * The expected values were measured on silicon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/misc/esp32s3_reg.h"
#include "hw/ssi/esp32s3_spi.h"
#include "esp-qtest.h"

#define PSRAM_OCT_READ       0x0000
#define PSRAM_OCT_WRITE      0x8080
#define PSRAM_SIZE           (8 * 1024 * 1024)

/* A 16-bit command and a 32-bit address, as IDF sends to an octal PSRAM */
static void psram_setup(QTestState *qts, uint32_t cmd, uint32_t addr,
                        uint32_t dummy_cycles, uint32_t user)
{
    uint32_t user1 = 0;
    uint32_t user2 = 0;

    user1 = FIELD_DP32(user1, SPI_MEM_USER1, USR_ADDR_BITLEN, 31);
    user1 = FIELD_DP32(user1, SPI_MEM_USER1, USR_DUMMY_CYCLELEN,
                       dummy_cycles - 1);
    user2 = FIELD_DP32(user2, SPI_MEM_USER2, USR_COMMAND_BITLEN, 15);
    user2 = FIELD_DP32(user2, SPI_MEM_USER2, USR_COMMAND_VALUE, cmd);

    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_USER,
                   R_SPI_MEM_USER_USR_COMMAND_MASK |
                   R_SPI_MEM_USER_USR_ADDR_MASK |
                   R_SPI_MEM_USER_USR_DUMMY_MASK | user);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_USER1, user1);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_USER2, user2);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_ADDR, addr);
}

static void psram_write(QTestState *qts, uint32_t addr, uint32_t value)
{
    psram_setup(qts, PSRAM_OCT_WRITE, addr, 8, R_SPI_MEM_USER_USR_MOSI_MASK);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_MOSI_DLEN, 31);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_W0, value);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_CMD,
                   R_SPI_MEM_CMD_USR_MASK);
}

static uint32_t psram_read(QTestState *qts, uint32_t addr)
{
    /* IDF programs 18 cycles for an octal read, OCT_PSRAM_RD_DUMMY_BITLEN */
    psram_setup(qts, PSRAM_OCT_READ, addr, 18, R_SPI_MEM_USER_USR_MISO_MASK);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_MISO_DLEN, 31);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_CMD,
                   R_SPI_MEM_CMD_USR_MASK);
    return esp_reg_readl(qts, DR_REG_SPI1_BASE, A_SPI_MEM_W0);
}

static QTestState *start_octal_psram(void)
{
    return qtest_init("-machine esp32s3 -m 8M "
                      "-global driver=ssi_psram,property=is_octal,value=true");
}

/*
 * By default the bus is the flash's, so a transaction must not reach the
 * PSRAM.
 */
static void test_psram_not_selected_by_default(void)
{
    QTestState *qts = start_octal_psram();

    psram_write(qts, 0x10, 0x12345678);
    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_MISC,
                   R_SPI_MEM_MISC_CS0_DIS_MASK);
    g_assert_cmphex(psram_read(qts, 0x10), ==, 0);
    qtest_quit(qts);
}

/*
 * An address with bit 31 set used to crash QEMU. esp32s3-aa20 wraps at the
 * size.
 */
static void test_psram_address_wraps(void)
{
    QTestState *qts = start_octal_psram();

    esp_reg_writel(qts, DR_REG_SPI1_BASE, A_SPI_MEM_MISC,
                   R_SPI_MEM_MISC_CS0_DIS_MASK);

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

static void register_spi_tests(void)
{
    if (qtest_has_machine("esp32s3")) {
        qtest_add_func("/esp/spi1/esp32s3/psram-not-selected-by-default",
                       test_psram_not_selected_by_default);
        qtest_add_func("/esp/psram/esp32s3/address-wraps",
                       test_psram_address_wraps);
    }
}

ESP_QTEST_SUITE(register_spi_tests)

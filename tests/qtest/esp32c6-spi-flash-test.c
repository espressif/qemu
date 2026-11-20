/*
 * QTest for flash reads through SPI1 on the ESP32-C6 and ESP32-H2 machines.
 * Each test programs SPI1 the way a piece of firmware does and checks that the
 * data comes back from the right flash address.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define SPI1_BASE            0x60003000
#define SPI_CMD              (SPI1_BASE + 0x00)
#define SPI_ADDR             (SPI1_BASE + 0x04)
#define SPI_CTRL             (SPI1_BASE + 0x08)
#define SPI_USER             (SPI1_BASE + 0x18)
#define SPI_USER1            (SPI1_BASE + 0x1c)
#define SPI_USER2            (SPI1_BASE + 0x20)
#define SPI_MISO_DLEN        (SPI1_BASE + 0x28)
#define SPI_RD_STATUS        (SPI1_BASE + 0x2c)
#define SPI_W0               (SPI1_BASE + 0x58)

#define SPI_CMD_USR          BIT(18)
#define SPI_USER_COMMAND     BIT(31)
#define SPI_USER_ADDR        BIT(30)
#define SPI_USER_DUMMY       BIT(29)
#define SPI_USER_MISO        BIT(28)
#define SPI_USER_CS_SETUP    BIT(7)
#define SPI_USER_CS_HOLD     BIT(6)

/* SPI_MEM_USER1: address phase bits and dummy cycles, both minus one */
#define USER1(addr_bits, dummy_cycles) \
    ((((addr_bits) - 1u) << 26) | ((dummy_cycles) - 1u))

/* SPI_MEM_CTRL as esp_rom_spiflash_read() sets it in DIO mode (FREAD_DIO) */
#define CTRL_DIO             0x00ac200c

/* With a 4 MB image, the machine attaches a gd25q32 */
#define FLASH_SIZE           (4 * 1024 * 1024)
#define READ_ADDR            0x10000
#define READ_LEN             16

static char *flash_path;

/* Not 0 at the start, so a byte of zeros inserted before the data shows */
static uint8_t pattern(uint32_t addr)
{
    return (addr & 0xff) ^ 0xa5;
}

static void make_flash_image(void)
{
    g_autofree uint8_t *buf = g_malloc(FLASH_SIZE);
    GError *err = NULL;
    int fd;

    for (uint32_t i = 0; i < FLASH_SIZE; i++) {
        buf[i] = pattern(i);
    }
    fd = g_file_open_tmp("esp32c6-flash-XXXXXX", &flash_path, &err);
    g_assert_no_error(err);
    g_assert_cmpint(write(fd, buf, FLASH_SIZE), ==, FLASH_SIZE);
    close(fd);
}

/* A 0xBB (dual I/O fast read) of READ_LEN bytes at READ_ADDR */
static void dio_read(const char *machine, uint32_t user1, bool dummy)
{
    QTestState *qts = qtest_initf("-machine %s "
                                  "-drive file=%s,if=mtd,format=raw",
                                  machine, flash_path);
    uint32_t user = SPI_USER_COMMAND | SPI_USER_ADDR | SPI_USER_MISO |
                    SPI_USER_CS_SETUP | SPI_USER_CS_HOLD;

    if (dummy) {
        user |= SPI_USER_DUMMY;
    }
    qtest_writel(qts, SPI_CTRL, CTRL_DIO);
    qtest_writel(qts, SPI_USER, user);
    qtest_writel(qts, SPI_USER1, user1);
    qtest_writel(qts, SPI_USER2, (7u << 28) | 0xbb);
    qtest_writel(qts, SPI_RD_STATUS, 0);         /* WB_MODE = 0 */
    qtest_writel(qts, SPI_MISO_DLEN, READ_LEN * 8 - 1);
    qtest_writel(qts, SPI_ADDR, READ_ADDR);
    qtest_writel(qts, SPI_CMD, SPI_CMD_USR);

    for (int i = 0; i < READ_LEN; i++) {
        uint32_t word = qtest_readl(qts, SPI_W0 + (i & ~3));

        g_assert_cmphex((word >> ((i & 3) * 8)) & 0xff, ==,
                        pattern(READ_ADDR + i));
    }
    qtest_quit(qts);
}

/*
 * esp_rom_spiflash_read() in DIO mode: a 28-bit address phase (the address and
 * the high 4 mode bits) and 2 dummy cycles (the low 4 mode bits).
 */
static void test_rom_dio_read(const void *machine)
{
    dio_read(machine, USER1(28, 2), true);
}

/* esp_flash_read() in DIO mode: a 32-bit phase, the address and WB_MODE */
static void test_idf_dio_read(const void *machine)
{
    dio_read(machine, USER1(32, 8), false);
}

/* A 24-bit address and 4 dummy cycles, as the C3 and S3 ROMs send it */
static void test_24bit_dio_read(const void *machine)
{
    dio_read(machine, USER1(24, 4), true);
}

static void add_tests(const char *machine)
{
    g_autofree char *rom = g_strdup_printf("/%s/spi1/rom_dio_read", machine);
    g_autofree char *idf = g_strdup_printf("/%s/spi1/idf_dio_read", machine);
    g_autofree char *b24 = g_strdup_printf("/%s/spi1/24bit_dio_read", machine);

    qtest_add_data_func(rom, machine, test_rom_dio_read);
    qtest_add_data_func(idf, machine, test_idf_dio_read);
    qtest_add_data_func(b24, machine, test_24bit_dio_read);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    make_flash_image();

    if (qtest_has_machine("esp32c6")) {
        add_tests("esp32c6");
    }
    if (qtest_has_machine("esp32h2")) {
        add_tests("esp32h2");
    }

    ret = g_test_run();
    unlink(flash_path);
    g_free(flash_path);
    return ret;
}

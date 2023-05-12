/*
 * ESP32-C3 eFuse emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c3_reg.h"
#include "sysemu/block-backend.h"
#include "qemu/error-report.h"

#define TYPE_ESP32C3_EFUSE "nvram.esp32c3.efuse"
#define ESP32C3_EFUSE(obj) OBJECT_CHECK(ESP32C3EfuseState, (obj), TYPE_ESP32C3_EFUSE)

#define ESP32C3_EFUSE_IO_RANGE_SIZE (sizeof(ESP32C3EfuseRegs))

/**
 * Size of the efuses in bytes on the ESP32C3
 */
#define ESP32C3_EFUSE_BYTE_COUNT        (4096/4)

/**
 * Number of data registers used when programming efuses
 */
#define ESP32C3_EFUSE_PGM_DATA_COUNT    8

/**
 * Number of check registers used when programming efuses
 */
#define ESP32C3_EFUSE_PGM_CHECK_COUNT   3

/**
 * Start address of the different BLOCKs in the ESP32C3EfuseRegs structure
 */
#define ESP32C3_EFUSE_BLOCK0_ADDR       0x002c
#define ESP32C3_EFUSE_BLOCK1_ADDR       0x0044
#define ESP32C3_EFUSE_BLOCK2_ADDR       0x005c
#define ESP32C3_EFUSE_BLOCK3_ADDR       0x007c
#define ESP32C3_EFUSE_BLOCK4_ADDR       0x009c
#define ESP32C3_EFUSE_BLOCK5_ADDR       0x00bc
#define ESP32C3_EFUSE_BLOCK6_ADDR       0x00dc
#define ESP32C3_EFUSE_BLOCK7_ADDR       0x00fc
#define ESP32C3_EFUSE_BLOCK8_ADDR       0x011c
#define ESP32C3_EFUSE_BLOCK9_ADDR       0x013c
#define ESP32C3_EFUSE_BLOCK10_ADDR      0x015c


/**
 * Size of BLOCK0 in 32-bit word unit
 */
#define ESP32C3_EFUSE_BLOCK0_WORDS      6

/**
 * Magic values for write and read operations
 */
#define EFUSE_WRITE_OPCODE  0x5A5A
#define EFUSE_READ_OPCODE   0x5AA5

typedef enum {
    EFUSE_BLOCK0 = 0,
    EFUSE_MAC_SPI_SYS_0 = 1,
    EFUSE_BLOCK_SYS_DATA = 2,
    EFUSE_BLOCK_USR_DATA = 3,
    EFUSE_BLOCK_KEY0 = 4,
    EFUSE_BLOCK_KEY1 = 5,
    EFUSE_BLOCK_KEY2 = 6,
    EFUSE_BLOCK_KEY3 = 7,
    EFUSE_BLOCK_KEY4 = 8,
    EFUSE_BLOCK_KEY5 = 9,
    EFUSE_BLOCK_KEY6 = 10,
    EFUSE_BLOCK_MAX,
} EfuseBlocks;

/* Structure definition taken from `esp32c3/soc/efuse_struct.h` and re-adapted with few arrays */
struct ESP32C3EfuseRegs {
    uint32_t pgm_data[ESP32C3_EFUSE_PGM_DATA_COUNT];             /*Registers that stores data to be programmed.*/
    uint32_t pgm_check[ESP32C3_EFUSE_PGM_CHECK_COUNT];           /*Registers that stores the RS code to be programmed.*/
    uint32_t rd_wr_dis;                                          /*BLOCK0 data register $n.*/
    union {
        struct {
            uint32_t rd_dis:                      7;             /*The value of RD_DIS.*/
            uint32_t rpt4_reserved5:              1;             /*Reserved*/
            uint32_t dis_icache:                  1;             /*The value of DIS_ICACHE.*/
            uint32_t dis_usb_jtag:                1;             /*The value of DIS_USB_JTAG.*/
            uint32_t dis_download_icache:         1;             /*The value of DIS_DOWNLOAD_ICACHE.*/
            uint32_t dis_usb_device:              1;             /*The value of DIS_USB_DEVICE.*/
            uint32_t dis_force_download:          1;             /*The value of DIS_FORCE_DOWNLOAD.*/
            uint32_t dis_usb:                     1;             /*The value of DIS_USB.*/
            uint32_t dis_can:                     1;             /*The value of DIS_CAN.*/
            uint32_t jtag_sel_enable:             1;             /*The value of JTAG_SEL_ENABLE.*/
            uint32_t soft_dis_jtag:               3;             /*The value of SOFT_DIS_JTAG.*/
            uint32_t dis_pad_jtag:                1;             /*The value of DIS_PAD_JTAG.*/
            uint32_t dis_download_manual_encrypt: 1;             /*The value of DIS_DOWNLOAD_MANUAL_ENCRYPT.*/
            uint32_t usb_drefh:                   2;             /*The value of USB_DREFH.*/
            uint32_t usb_drefl:                   2;             /*The value of USB_DREFL.*/
            uint32_t usb_exchg_pins:              1;             /*The value of USB_EXCHG_PINS.*/
            uint32_t vdd_spi_as_gpio:             1;             /*The value of VDD_SPI_AS_GPIO.*/
            uint32_t btlc_gpio_enable:            2;             /*The value of BTLC_GPIO_ENABLE.*/
            uint32_t powerglitch_en:              1;             /*The value of POWERGLITCH_EN.*/
            uint32_t power_glitch_dsense:         2;             /*The value of POWER_GLITCH_DSENSE.*/
        };
        uint32_t val;
    } rd_repeat_data0;
    union {
        struct {
            uint32_t rpt4_reserved2:         16;                 /*Reserved.*/
            uint32_t wdt_delay_sel:           2;                 /*The value of WDT_DELAY_SEL.*/
            uint32_t spi_boot_crypt_cnt:      3;                 /*The value of SPI_BOOT_CRYPT_CNT.*/
            uint32_t secure_boot_key_revoke0: 1;                 /*The value of SECURE_BOOT_KEY_REVOKE0.*/
            uint32_t secure_boot_key_revoke1: 1;                 /*The value of SECURE_BOOT_KEY_REVOKE1.*/
            uint32_t secure_boot_key_revoke2: 1;                 /*The value of SECURE_BOOT_KEY_REVOKE2.*/
            uint32_t key_purpose_0:           4;                 /*The value of KEY_PURPOSE_0.*/
            uint32_t key_purpose_1:           4;                 /*The value of KEY_PURPOSE_1.*/
        };
        uint32_t val;
    } rd_repeat_data1;
    union {
        struct {
            uint32_t key_purpose_2:                 4;           /*The value of KEY_PURPOSE_2.*/
            uint32_t key_purpose_3:                 4;           /*The value of KEY_PURPOSE_3.*/
            uint32_t key_purpose_4:                 4;           /*The value of KEY_PURPOSE_4.*/
            uint32_t key_purpose_5:                 4;           /*The value of KEY_PURPOSE_5.*/
            uint32_t rpt4_reserved3:                4;           /*Reserved.*/
            uint32_t secure_boot_en:                1;           /*The value of SECURE_BOOT_EN.*/
            uint32_t secure_boot_aggressive_revoke: 1;           /*The value of SECURE_BOOT_AGGRESSIVE_REVOKE.*/
            uint32_t rpt4_reserved0:                6;           /*Reserved.*/
            uint32_t flash_tpuw:                    4;           /*The value of FLASH_TPUW.*/
        };
        uint32_t val;
    } rd_repeat_data2;
    union {
        struct {
            uint32_t dis_download_mode:        1;                /*The value of DIS_DOWNLOAD_MODE.*/
            uint32_t dis_direct_boot:          1;                /*The value of DIS_DIRECT_BOOT.*/
            uint32_t dis_usb_serial_jtag_rom_print:1;                /*The value of DIS_USB_SERIAL_JTAG_ROM_PRINT.*/
            uint32_t rpt4_reserved8:           1;                /*Reserved.*/
            uint32_t dis_usb_serial_jtag_download_mode:    1;    /*The value of dis_usb_serial_jtag_download_mode.*/
            uint32_t enable_security_download: 1;                /*The value of ENABLE_SECURITY_DOWNLOAD.*/
            uint32_t uart_print_control:       2;                /*The value of UART_PRINT_CONTROL.*/
            uint32_t rpt4_reserved7:           5;                /*Reserved.*/
            uint32_t force_send_resume:        1;                /*The value of FORCE_SEND_RESUME.*/
            uint32_t secure_version:          16;                /*The value of SECURE_VERSION.*/
            uint32_t rpt4_reserved1:           1;                /*Reserved.*/
            uint32_t err_rst_enable:           1;                /*Use BLOCK0 to check error record registers, 0 - without check.*/
        };
        uint32_t val;
    } rd_repeat_data3;
    union {
        struct {
            uint32_t disable_wafer_version_major: 1;
            uint32_t disable_blk_version_major: 1;
            uint32_t rpt4_reserved4:22;                          /*Reserved.*/
            uint32_t reserved24:     8;                          /*Reserved.*/
        };
        uint32_t val;
    } rd_repeat_data4;
    uint32_t rd_mac_spi_sys_0;                                   /*BLOCK1 data register $n.*/
    union {
        struct {
            uint32_t mac_1:         16;                          /*Stores the high 16 bits of MAC address.*/
            uint32_t spi_pad_conf_0:16;                          /*Stores the zeroth part of SPI_PAD_CONF.*/
        };
        uint32_t val;
    } rd_mac_spi_sys_1;
    uint32_t rd_mac_spi_sys_2;                                   /*BLOCK1 data register $n.*/
    union {
        struct {
            uint32_t spi_pad_conf_2:  18;                        /*Stores the second part of SPI_PAD_CONF.*/
            uint32_t wafer_version_minor_low:    3;
            uint32_t pkg_version:      3;
            uint32_t blk_version_minor:3;
            uint32_t sys_data_part0_0: 5;
        };
        uint32_t val;
    } rd_mac_spi_sys_3;
    union {
        struct {
            uint32_t reserved1:              7;
            uint32_t k_rtc_ldo:              7;
            uint32_t k_dig_ldo:              7;
            uint32_t v_rtc_dbias20:          8;
            uint32_t v_dig_dbias20_low:      3;
        };
        uint32_t val;
    } rd_mac_spi_sys_4;                                   /*BLOCK1 data register $n.*/
    union {
        struct {
            uint32_t v_dig_dbias20_hi:      5;
            uint32_t dig_dbias_hvt:         5;
            uint32_t reserved1:             13;
            uint32_t wafer_version_minor_high:    1;
            uint32_t wafer_version_major:    2;
            uint32_t reserved2:              6;
        };
        uint32_t val;
    } rd_mac_spi_sys_5;                                          /*BLOCK1 data register $n.*/
    uint32_t rd_sys_part1_data0;                                 /*Register $n of BLOCK2 (system).*/
    uint32_t rd_sys_part1_data1;                                 /*Register $n of BLOCK2 (system).*/
    uint32_t rd_sys_part1_data2;                                 /*Register $n of BLOCK2 (system).*/
    uint32_t rd_sys_part1_data3;                                 /*Register $n of BLOCK2 (system).*/
    union {
        struct {
            uint32_t blk_version_major:     2;
            uint32_t reserved1:             10;
            uint32_t ocode:                 8;
            uint32_t reserved2:             12;
        };
        uint32_t val;
    } rd_sys_part1_data4;                                        /*Register $n of BLOCK2 (system).*/
    uint32_t rd_sys_part1_data5;                                 /*Register $n of BLOCK2 (system).*/
    uint32_t rd_sys_part1_data6;                                 /*Register $n of BLOCK2 (system).*/
    uint32_t rd_sys_part1_data7;                                 /*Register $n of BLOCK2 (system).*/
    uint32_t rd_usr_data0;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_usr_data1;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_usr_data2;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_usr_data3;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_usr_data4;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_usr_data5;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_usr_data6;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_usr_data7;                                       /*Register $n of BLOCK3 (user).*/
    uint32_t rd_key0_data0;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key0_data1;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key0_data2;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key0_data3;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key0_data4;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key0_data5;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key0_data6;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key0_data7;                                      /*Register $n of BLOCK4 (KEY0).*/
    uint32_t rd_key1_data0;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key1_data1;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key1_data2;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key1_data3;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key1_data4;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key1_data5;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key1_data6;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key1_data7;                                      /*Register $n of BLOCK5 (KEY1).*/
    uint32_t rd_key2_data0;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key2_data1;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key2_data2;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key2_data3;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key2_data4;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key2_data5;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key2_data6;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key2_data7;                                      /*Register $n of BLOCK6 (KEY2).*/
    uint32_t rd_key3_data0;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key3_data1;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key3_data2;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key3_data3;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key3_data4;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key3_data5;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key3_data6;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key3_data7;                                      /*Register $n of BLOCK7 (KEY3).*/
    uint32_t rd_key4_data0;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key4_data1;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key4_data2;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key4_data3;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key4_data4;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key4_data5;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key4_data6;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key4_data7;                                      /*Register $n of BLOCK8 (KEY4).*/
    uint32_t rd_key5_data0;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_key5_data1;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_key5_data2;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_key5_data3;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_key5_data4;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_key5_data5;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_key5_data6;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_key5_data7;                                      /*Register $n of BLOCK9 (KEY5).*/
    uint32_t rd_sys_part2_data0;                                 /*Register $n of BLOCK10 (system).*/
    uint32_t rd_sys_part2_data1;                                 /*Register $n of BLOCK10 (system).*/
    uint32_t rd_sys_part2_data2;                                 /*Register $n of BLOCK10 (system).*/
    uint32_t rd_sys_part2_data3;                                 /*Register $n of BLOCK10 (system).*/
    uint32_t rd_sys_part2_data4;                                 /*Register $n of BLOCK10 (system).*/
    uint32_t rd_sys_part2_data5;                                 /*Register $n of BLOCK10 (system).*/
    uint32_t rd_sys_part2_data6;                                 /*Register $n of BLOCK10 (system).*/
    uint32_t rd_sys_part2_data7;                                 /*Register $n of BLOCK10 (system).*/
    union {
        struct {
            uint32_t rd_dis_err:                      7;         /*If any bit in RD_DIS is 1  then it indicates a programming error.*/
            uint32_t rpt4_reserved5_err:              1;         /*Reserved.*/
            uint32_t dis_icache_err:                  1;         /*If DIS_ICACHE is 1  then it indicates a programming error.*/
            uint32_t dis_usb_jtag_err:                1;         /*If DIS_USB_JTAG is 1  then it indicates a programming error.*/
            uint32_t dis_download_icache:             1;         /*If DIS_DOWNLOAD_ICACHE is 1  then it indicates a programming error.*/
            uint32_t dis_usb_device_err:              1;         /*If DIS_USB_DEVICE is 1  then it indicates a programming error.*/
            uint32_t dis_force_download_err:          1;         /*If DIS_FORCE_DOWNLOAD is 1  then it indicates a programming error.*/
            uint32_t dis_usb_err:                     1;         /*If DIS_USB is 1  then it indicates a programming error.*/
            uint32_t dis_can_err:                     1;         /*If DIS_CAN is 1  then it indicates a programming error.*/
            uint32_t jtag_sel_enable_err:             1;         /*If JTAG_SEL_ENABLE is 1  then it indicates a programming error.*/
            uint32_t soft_dis_jtag_err:               3;         /*If SOFT_DIS_JTAG is 1  then it indicates a programming error.*/
            uint32_t dis_pad_jtag_err:                1;         /*If DIS_PAD_JTAG is 1  then it indicates a programming error.*/
            uint32_t dis_download_manual_encrypt_err: 1;         /*If DIS_DOWNLOAD_MANUAL_ENCRYPT is 1  then it indicates a programming error.*/
            uint32_t usb_drefh_err:                   2;         /*If any bit in USB_DREFH is 1  then it indicates a programming error.*/
            uint32_t usb_drefl_err:                   2;         /*If any bit in USB_DREFL is 1  then it indicates a programming error.*/
            uint32_t usb_exchg_pins_err:              1;         /*If USB_EXCHG_PINS is 1  then it indicates a programming error.*/
            uint32_t vdd_spi_as_gpio_err:             1;         /*If VDD_SPI_AS_GPIO is 1  then it indicates a programming error.*/
            uint32_t btlc_gpio_enable_err:            2;         /*If any bit in BTLC_GPIO_ENABLE is 1  then it indicates a programming error.*/
            uint32_t powerglitch_en_err:              1;         /*If POWERGLITCH_EN is 1  then it indicates a programming error.*/
            uint32_t power_glitch_dsense_err:         2;         /*If any bit in POWER_GLITCH_DSENSE is 1  then it indicates a programming error.*/
        };
        uint32_t val;
    } rd_repeat_err0;
    union {
        struct {
            uint32_t rpt4_reserved2_err:         16;             /*Reserved.*/
            uint32_t wdt_delay_sel_err:           2;             /*If any bit in WDT_DELAY_SEL is 1  then it indicates a programming error.*/
            uint32_t spi_boot_crypt_cnt_err:      3;             /*If any bit in SPI_BOOT_CRYPT_CNT is 1  then it indicates a programming error.*/
            uint32_t secure_boot_key_revoke0_err: 1;             /*If SECURE_BOOT_KEY_REVOKE0 is 1  then it indicates a programming error.*/
            uint32_t secure_boot_key_revoke1_err: 1;             /*If SECURE_BOOT_KEY_REVOKE1 is 1  then it indicates a programming error.*/
            uint32_t secure_boot_key_revoke2_err: 1;             /*If SECURE_BOOT_KEY_REVOKE2 is 1  then it indicates a programming error.*/
            uint32_t key_purpose_0_err:           4;             /*If any bit in KEY_PURPOSE_0 is 1  then it indicates a programming error.*/
            uint32_t key_purpose_1_err:           4;             /*If any bit in KEY_PURPOSE_1 is 1  then it indicates a programming error.*/
        };
        uint32_t val;
    } rd_repeat_err1;
    union {
        struct {
            uint32_t key_purpose_2_err:                 4;       /*If any bit in KEY_PURPOSE_2 is 1  then it indicates a programming error.*/
            uint32_t key_purpose_3_err:                 4;       /*If any bit in KEY_PURPOSE_3 is 1  then it indicates a programming error.*/
            uint32_t key_purpose_4_err:                 4;       /*If any bit in KEY_PURPOSE_4 is 1  then it indicates a programming error.*/
            uint32_t key_purpose_5_err:                 4;       /*If any bit in KEY_PURPOSE_5 is 1  then it indicates a programming error.*/
            uint32_t rpt4_reserved3_err:                4;       /*Reserved.*/
            uint32_t secure_boot_en_err:                1;       /*If SECURE_BOOT_EN is 1  then it indicates a programming error.*/
            uint32_t secure_boot_aggressive_revoke_err: 1;       /*If SECURE_BOOT_AGGRESSIVE_REVOKE is 1  then it indicates a programming error.*/
            uint32_t rpt4_reserved0_err:                6;       /*Reserved.*/
            uint32_t flash_tpuw_err:                    4;       /*If any bit in FLASH_TPUM is 1  then it indicates a programming error.*/
        };
        uint32_t val;
    } rd_repeat_err2;
    union {
        struct {
            uint32_t dis_download_mode_err:        1;            /*If the value is not zero then it indicates a programming error on DIS_DOWNLOAD_MODE.*/
            uint32_t dis_direct_boot_err:          1;            /*If the value is not zero then it indicates a programming error on DIS_DIRECT_BOOT.*/
            uint32_t dis_usb_serial_jtag_rom_print_err:1;            /*If the value is not zero then it indicates a programming error on DIS_USB_SERIAL_JTAG_ROM_PRINT.*/
            uint32_t rpt4_reserved8_err:           1;            /*Reserved.*/
            uint32_t dis_usb_serial_jtag_download_mode_err:    1; /*If the value is not zero then it indicates a programming error on DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE.*/
            uint32_t enable_security_download_err: 1;            /*If the value is not zero then it indicates a programming error on ENABLE_SECURITY_DOWNLOAD.*/
            uint32_t uart_print_control_err:       2;            /*If the value is not zero  then it indicates a programming error on UART_PRINT_CONTROL.*/
            uint32_t rpt4_reserved7_err:           5;            /*Reserved*/
            uint32_t force_send_resume_err:        1;            /*If the value is not zero then it indicates a programming error on FORCE_SEND_RESUME.*/
            uint32_t secure_version_err:          16;            /*If the value is not zero then it indicates a programming error on SECURE_VERSION.*/
            uint32_t rpt4_reserved1_err:           1;            /*Reserved.*/
            uint32_t err_rst_enable_err:           1;            /*Use BLOCK0 to check error record registers, 0 - without check.*/
        };
        uint32_t val;
    } rd_repeat_err3;
    union {
        struct {
            uint32_t rpt4_reserved4_err:24;                      /*Reserved.*/
            uint32_t reserved24:         8;                      /*Reserved.*/
        };
        uint32_t val;
    } rd_repeat_err4;
    uint32_t reserved_190;
    uint32_t reserved_194;
    uint32_t reserved_198;
    uint32_t reserved_19c;
    uint32_t reserved_1a0;
    uint32_t reserved_1a4;
    uint32_t reserved_1a8;
    uint32_t reserved_1ac;
    uint32_t reserved_1b0;
    uint32_t reserved_1b4;
    uint32_t reserved_1b8;
    uint32_t reserved_1bc;
    union {
        struct {
            uint32_t mac_spi_8m_err_num: 3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t reserved3:          1;                      /*Reserved.*/
            uint32_t sys_part1_num:      3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t mac_spi_8m_fail:    1;                      /*0: Means no failure and that the data of MAC_SPI_8M is reliable 1: Means that programming MAC_SPI_8M failed and the number of error bytes is over 6.*/
            uint32_t usr_data_err_num:   3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t sys_part1_fail:     1;                      /*0: Means no failure and that the data of system part1 is reliable 1: Means that programming the data of system part1 failed and the number of error bytes is over 6.*/
            uint32_t key0_err_num:       3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t usr_data_fail:      1;                      /*0: Means no failure and that the data of user data is reliable 1: Means that programming user data failed and the number of error bytes is over 6.*/
            uint32_t key1_err_num:       3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t key0_fail:          1;                      /*0: Means no failure and that the data of key0 is reliable 1: Means that programming key0 failed and the number of error bytes is over 6.*/
            uint32_t key2_err_num:       3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t key1_fail:          1;                      /*0: Means no failure and that the data of key1 is reliable 1: Means that programming key1 failed and the number of error bytes is over 6.*/
            uint32_t key3_err_num:       3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t key2_fail:          1;                      /*0: Means no failure and that the data of key2 is reliable 1: Means that programming key2 failed and the number of error bytes is over 6.*/
            uint32_t key4_err_num:       3;                      /*The value of this signal means the number of error bytes.*/
            uint32_t key3_fail:          1;                      /*0: Means no failure and that the data of key3 is reliable 1: Means that programming key3 failed and the number of error bytes is over 6.*/
        };
        uint32_t val;
    } rd_rs_err0;
    union {
        struct {
            uint32_t key5_err_num:      3;                       /*The value of this signal means the number of error bytes.*/
            uint32_t key4_fail:         1;                       /*0: Means no failure and that the data of KEY4 is reliable 1: Means that programming KEY4 failed and the number of error bytes is over 6.*/
            uint32_t sys_part2_err_num: 3;                       /*The value of this signal means the number of error bytes.*/
            uint32_t key5_fail:         1;                       /*0: Means no failure and that the data of KEY5 is reliable 1: Means that programming KEY5 failed and the number of error bytes is over 6.*/
            uint32_t reserved8:        24;                       /*Reserved.*/
        };
        uint32_t val;
    } rd_rs_err1;
    union {
        struct {
            uint32_t mem_force_pd:     1;                        /*Set this bit to force eFuse SRAM into power-saving mode.*/
            uint32_t mem_clk_force_on: 1;                        /*Set this bit and force to activate clock signal of eFuse SRAM.*/
            uint32_t mem_force_pu:     1;                        /*Set this bit to force eFuse SRAM into working mode.*/
            uint32_t reserved3:       13;                        /*Reserved.*/
            uint32_t clk_en:           1;                        /*Set this bit and force to enable clock signal of eFuse memory.*/
            uint32_t reserved17:      15;                        /*Reserved.*/
        };
        uint32_t val;
    } clk;
    union {
        struct {
            uint32_t op_code:   16;                              /*0x5A5A: Operate programming command 0x5AA5: Operate read command.*/
            uint32_t reserved16:16;                              /*Reserved.*/
        };
        uint32_t val;
    } conf;
    union {
        struct {
            uint32_t state:            4;                        /*Indicates the state of the eFuse state machine.*/
            uint32_t otp_load_sw:      1;                        /*The value of OTP_LOAD_SW.*/
            uint32_t otp_vddq_c_sync2: 1;                        /*The value of OTP_VDDQ_C_SYNC2.*/
            uint32_t otp_strobe_sw:    1;                        /*The value of OTP_STROBE_SW.*/
            uint32_t otp_csb_sw:       1;                        /*The value of OTP_CSB_SW.*/
            uint32_t otp_pgenb_sw:     1;                        /*The value of OTP_PGENB_SW.*/
            uint32_t otp_vddq_is_sw:   1;                        /*The value of OTP_VDDQ_IS_SW.*/
            uint32_t repeat_err_cnt:   8;                        /*Indicates the number of error bits during programming BLOCK0.*/
            uint32_t reserved18:      14;                        /*Reserved.*/
        };
        uint32_t val;
    } status;
    union {
        struct {
            uint32_t read_cmd:   1;                              /*Set this bit to send read command.*/
            uint32_t pgm_cmd:    1;                              /*Set this bit to send programming command.*/
            uint32_t blk_num:    4;                              /*The serial number of the block to be programmed. Value 0-10 corresponds to block number 0-10  respectively.*/
            uint32_t reserved6: 26;                              /*Reserved.*/
        };
        uint32_t val;
    } cmd;
    union {
        struct {
            uint32_t read_done:         1;                       /*The raw bit signal for read_done interrupt.*/
            uint32_t pgm_done:          1;                       /*The raw bit signal for pgm_done interrupt.*/
            uint32_t reserved2:        30;                       /*Reserved.*/
        };
        uint32_t val;
    } int_raw;
    union {
        struct {
            uint32_t read_done:        1;                        /*The status signal for read_done interrupt.*/
            uint32_t pgm_done:         1;                        /*The status signal for pgm_done interrupt.*/
            uint32_t reserved2:       30;                        /*Reserved.*/
        };
        uint32_t val;
    } int_st;
    union {
        struct {
            uint32_t read_done:         1;                       /*The enable signal for read_done interrupt.*/
            uint32_t pgm_done:          1;                       /*The enable signal for pgm_done interrupt.*/
            uint32_t reserved2:        30;                       /*Reserved.*/
        };
        uint32_t val;
    } int_ena;
    union {
        struct {
            uint32_t read_done:         1;                       /*The clear signal for read_done interrupt.*/
            uint32_t pgm_done:          1;                       /*The clear signal for pgm_done interrupt.*/
            uint32_t reserved2:        30;                       /*Reserved.*/
        };
        uint32_t val;
    } int_clr;
    union {
        struct {
            uint32_t dac_clk_div:     8;                         /*Controls the division factor of the rising clock of the programming voltage.*/
            uint32_t dac_clk_pad_sel: 1;                         /*Don't care.*/
            uint32_t dac_num:         8;                         /*Controls the rising period of the programming voltage.*/
            uint32_t oe_clr:          1;                         /*Reduces the power supply of the programming voltage.*/
            uint32_t reserved18:     14;                         /*Reserved.*/
        };
        uint32_t val;
    } dac_conf;
    union {
        struct {
            uint32_t reserved0:    24;                           /*Configures the setup time of read operation.*/
            uint32_t read_init_num: 8;                           /*Configures the initial read time of eFuse.*/
        };
        uint32_t val;
    } rd_tim_conf;
    union {
        struct {
            uint32_t reserved0:  8;                              /*Configures the setup time of programming operation.*/
            uint32_t pwr_on_num:16;                              /*Configures the power up time for VDDQ.*/
            uint32_t reserved24: 8;                              /*Reserved.*/
        };
        uint32_t val;
    } wr_tim_conf1;
    union {
        struct {
            uint32_t pwr_off_num:16;                             /*Configures the power outage time for VDDQ.*/
            uint32_t reserved16: 16;                             /*Reserved.*/
        };
        uint32_t val;
    } wr_tim_conf2;
    uint32_t reserved_1f8;
    union {
        struct {
            uint32_t date:      28;                              /*Stores eFuse version.*/
            uint32_t reserved28: 4;                              /*Reserved.*/
        };
        uint32_t val;
    } date;
    uint32_t dbg_erase_all;
} __attribute__((packed));

typedef struct ESP32C3EfuseRegs ESP32C3EfuseRegs;

typedef struct ESP32C3EfuseState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    /* In case no block was given by the user, use the mirror as a file in RAM */
    BlockBackend *blk;
    void* mirror;

    qemu_irq irq;
    /* Use a mirror to make sure the operation value did not change between the moment
     * it is scheduled and the moment it actually happens. */
    uint32_t op_cmd_mirror;
    QEMUTimer op_timer;

    ESP32C3EfuseRegs efuses;
} ESP32C3EfuseState;


/**
 * Define a few helpers for the efuse blocks
 */

/**
 * Returns the size of the given block, in bytes.
 * BLOCK0 and BLOCK1 have 6 registers while other blocks have 8 registers
 */
static inline int esp32c3_efuse_block_size(const uint32_t block)
{
    return (block == 0 || block == 1) ? ESP32C3_EFUSE_BLOCK0_WORDS * sizeof(uint32_t)
                                      : 8 * sizeof(uint32_t);
}

/**
 * Returns true if the current command is a valid read command, false else
 */
static inline bool esp32c3_efuse_is_read_cmd(ESP32C3EfuseState *s)
{
    return s->efuses.conf.op_code == EFUSE_READ_OPCODE && s->efuses.cmd.read_cmd;
}

/**
 * Returns true if the current command is a valid write/program command, false else
 */
static inline bool esp32c3_efuse_is_write_cmd(ESP32C3EfuseState *s)
{
    return s->efuses.conf.op_code == EFUSE_WRITE_OPCODE && s->efuses.cmd.pgm_cmd;
}

/**
 * @brief Returns the key purpose of the given efuse key block number
 */
static inline uint32_t esp32c3_efuse_get_key_purpose(ESP32C3EfuseState *s, const int efuse_block_num)
{
    uint32_t purpose = -1;
    switch (efuse_block_num) {
        case EFUSE_BLOCK_KEY0:
            purpose = s->efuses.rd_repeat_data1.key_purpose_0;
            break;
        case EFUSE_BLOCK_KEY1:
            purpose = s->efuses.rd_repeat_data1.key_purpose_1;
            break;
        case EFUSE_BLOCK_KEY2:
            purpose = s->efuses.rd_repeat_data2.key_purpose_2;
            break;
        case EFUSE_BLOCK_KEY3:
            purpose = s->efuses.rd_repeat_data2.key_purpose_3;
            break;
        case EFUSE_BLOCK_KEY4:
            purpose = s->efuses.rd_repeat_data2.key_purpose_4;
            break;
        case EFUSE_BLOCK_KEY5:
            purpose = s->efuses.rd_repeat_data2.key_purpose_5;
            break;
        default:
            error_report("[Efuse] Out of range key block specified: %d", efuse_block_num);
            break;
    }

    return purpose;
}


static inline void esp32c3_efuse_get_key(ESP32C3EfuseState *s, const uint8_t efuse_block_num, uint8_t* efuse_key)
{
    if (efuse_block_num < EFUSE_BLOCK_KEY0 || efuse_block_num > EFUSE_BLOCK_KEY6) {
        error_report("[Efuse] Out of range key block specified: %d", efuse_block_num);
    } else {
        memcpy(efuse_key, &s->efuses.rd_key0_data0 + (efuse_block_num - EFUSE_BLOCK_KEY0) * 8, 32);
    }
}

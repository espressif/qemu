/*
 * ESP32-S3 registers
 *
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#define DR_REG_FRAMEBUF_BASE                    0x21000000

#define DR_REG_UART_BASE                        0x60000000
#define DR_REG_SPI1_BASE                        0x60002000
#define DR_REG_SPI0_BASE                        0x60003000
#define DR_REG_GPIO_BASE                        0x60004000
#define DR_REG_GPIO_SD_BASE                     0x60004f00
#define DR_REG_FE2_BASE                         0x60005000
#define DR_REG_FE_BASE                          0x60006000
#define DR_REG_EFUSE_BASE                       0x60007000
#define DR_REG_RTCCNTL_BASE                     0x60008000
#define DR_REG_RTCIO_BASE                       0x60008400
#define DR_REG_SENS_BASE                        0x60008800
#define DR_REG_RTC_I2C_BASE                     0x60008C00
#define DR_REG_IO_MUX_BASE                      0x60009000
#define DR_REG_HINF_BASE                        0x6000B000
#define DR_REG_UHCI1_BASE                       0x6000C000
#define DR_REG_I2S_BASE                         0x6000F000
#define DR_REG_UART1_BASE                       0x60010000
#define DR_REG_BT_BASE                          0x60011000
#define DR_REG_I2C_EXT_BASE                     0x60013000
#define DR_REG_UHCI0_BASE                       0x60014000
#define DR_REG_SLCHOST_BASE                     0x60015000
#define DR_REG_RMT_BASE                         0x60016000
#define DR_REG_PCNT_BASE                        0x60017000
#define DR_REG_SLC_BASE                         0x60018000
#define DR_REG_LEDC_BASE                        0x60019000
#define DR_REG_NRX_BASE                         0x6001CC00
#define DR_REG_BB_BASE                          0x6001D000
#define DR_REG_PWM0_BASE                        0x6001E000
#define DR_REG_TIMERGROUP0_BASE                 0x6001F000
#define DR_REG_TIMERGROUP1_BASE                 0x60020000
#define DR_REG_RTC_SLOWMEM_BASE                 0x60021000
#define DR_REG_SYSTIMER_BASE                    0x60023000
#define DR_REG_SPI2_BASE                        0x60024000
#define DR_REG_SPI3_BASE                        0x60025000
#define DR_REG_SYSCON_BASE                      0x60026000
#define DR_REG_APB_CTRL_BASE                    0x60026000 /* Old name for SYSCON, to be removed */
#define DR_REG_I2C1_EXT_BASE                    0x60027000
#define DR_REG_SDMMC_BASE                       0x60028000
#define DR_REG_PERI_BACKUP_BASE                 0x6002A000
#define DR_REG_TWAI_BASE                        0x6002B000
#define DR_REG_PWM1_BASE                        0x6002C000
#define DR_REG_I2S1_BASE                        0x6002D000
#define DR_REG_UART2_BASE                       0x6002E000
#define DR_REG_USB_SERIAL_JTAG_BASE             0x60038000
#define DR_REG_USB_WRAP_BASE                    0x60039000
#define DR_REG_USB_OTG_BASE                     0x60080000
#define DR_REG_AES_BASE                         0x6003A000
#define DR_REG_SHA_BASE                         0x6003B000
#define DR_REG_RSA_BASE                         0x6003C000
#define DR_REG_HMAC_BASE                        0x6003E000
#define DR_REG_DIGITAL_SIGNATURE_BASE           0x6003D000
#define DR_REG_GDMA_BASE                        0x6003F000
#define DR_REG_APB_SARADC_BASE                  0x60040000
#define DR_REG_LCD_CAM_BASE                     0x60041000
#define DR_REG_SYSTEM_BASE                      0x600C0000
#define DR_REG_SENSITIVE_BASE                   0x600C1000
#define DR_REG_INTERRUPT_BASE                   0x600C2000
#define DR_REG_AES_XTS_BASE                     0x600CC000
#define DR_REG_EXTMEM_BASE                      0x600C4000
#define DR_REG_MMU_TABLE                        0x600C5000
#define DR_REG_ASSIST_DEBUG_BASE                0x600CE000
#define DR_REG_WCL_BASE                         0x600D0000
#define ESP_CACHE_TEMP_ADDR                     0x3C800000
#define DR_REG_EMAC_BASE                        0x600CD000
#define DR_REG_WDEV_BASE                        0x3ff75000


#define ESP32S3_IO_START_ADDR                   (DR_REG_UART_BASE)


#define APB_REG_BASE                            0x60000000


#define ESP32S3_DPORT_CROSSCORE_INT_COUNT     4
#define ESP32S3_INT_MATRIX_OUTPUTS    32
#define ESP32S3_CPU_COUNT             2
#define ESP32S3_UART_COUNT            3
#define ESP32S3_FRC_COUNT             2
#define ESP32S3_TIMG_COUNT            2
#define ESP32S3_SPI_COUNT             4
#define ESP32S3_I2C_COUNT             2
#define ESP32S3_RTC_CNTL_SCRATCH_REG_COUNT     8

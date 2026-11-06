/*
 * ESP32-C5 register base addresses and SoC definitions
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

/* Peripheral 0 register base addresses */
#define DR_REG_UART_BASE                        0x60000000
#define DR_REG_UART1_BASE                       0x60001000
#define DR_REG_SPI1_BASE                        0x60003000
#define DR_REG_TIMERGROUP0_BASE                 0x60008000
#define DR_REG_TIMERGROUP1_BASE                 0x60009000
#define DR_REG_SYSTIMER_BASE                    0x6000A000
#define DR_REG_TWAI_BASE                        0x6000B000
#define DR_REG_USB_SERIAL_JTAG_BASE             0x6000F000
#define DR_REG_INTERRUPT_BASE                   0x60010000

/* Peripheral 1 register base addresses */
#define DR_REG_GDMA_BASE                        0x60080000
#define DR_REG_AES_BASE                         0x60088000
#define DR_REG_SHA_BASE                         0x60089000
#define DR_REG_ECC_MULT_BASE                    0x6008B000
#define DR_REG_ECDSA_BASE                       0x6008E000
#define DR_REG_RSA_BASE                         0x6008A000
#define DR_REG_HMAC_BASE                        0x6008D000
#define DR_REG_DIGITAL_SIGNATURE_BASE           0x6008C000

/* HP Top peripherals */
#define DR_REG_IO_MUX_BASE                      0x60090000
#define DR_REG_GPIO_BASE                        0x60091000
#define DR_REG_SYSTEM_BASE                      0x60095000
#define DR_REG_SYSCON_BASE                      0x60096000

/* LP System (RTC) peripherals */
#define DR_REG_EFUSE_BASE                       0x600B4800
#define DR_REG_RTCCNTL_BASE                     0x600B1C00
#define DR_REG_RTC_I2C_BASE                     0x600B1800

/* CPU peripherals */
#define DR_REG_ASSIST_DEBUG_BASE                0x600C2000
#define DR_REG_EXTMEM_BASE                      0x600C8000
#define DR_REG_INTPRI_BASE                      0x600C5000

/* CLIC interrupt controller */
#define DR_REG_CLIC_BASE                        0x20800000
#define DR_REG_CLIC_CTRL_BASE                   0x20801000

/* Emulated ethernet (OpenCores) */
#define DR_REG_EMAC_BASE                        0x600CD000

/* SoC constants */
#define ESP32C5_IO_START_ADDR                   (0x60000000)
#define ESP32C5_UART_COUNT                      2
#define ESP32C5_TWAI_COUNT                      2

/* ESP32-C5 interrupt source numbers (from IDF soc/esp32c5/include/soc/interrupts.h) */
#define C5_ETS_EFUSE_INTR_SOURCE                14
#define C5_ETS_FROM_CPU_INTR0_SOURCE            23
#define C5_ETS_GPIO_INTR_SOURCE                 31
#define C5_ETS_UART0_INTR_SOURCE                47
#define C5_ETS_UART1_INTR_SOURCE                48
#define C5_ETS_USB_SERIAL_JTAG_INTR_SOURCE      54
#define C5_ETS_TG0_T0_LEVEL_INTR_SOURCE         57
#define C5_ETS_TG0_WDT_LEVEL_INTR_SOURCE        58
#define C5_ETS_TG1_T0_LEVEL_INTR_SOURCE         59
#define C5_ETS_TG1_WDT_LEVEL_INTR_SOURCE        60
#define C5_ETS_SYSTIMER_TARGET0_INTR_SOURCE      61
#define C5_ETS_SYSTIMER_TARGET1_INTR_SOURCE      62
#define C5_ETS_SYSTIMER_TARGET2_INTR_SOURCE      63
#define C5_ETS_DMA_IN_CH0_INTR_SOURCE           71
#define C5_ETS_DMA_IN_CH1_INTR_SOURCE           72
#define C5_ETS_DMA_IN_CH2_INTR_SOURCE           73
#define C5_ETS_DMA_OUT_CH0_INTR_SOURCE          74
#define C5_ETS_DMA_OUT_CH1_INTR_SOURCE          75
#define C5_ETS_DMA_OUT_CH2_INTR_SOURCE          76
#define C5_ETS_SPI2_INTR_SOURCE                 77
#define C5_ETS_SHA_INTR_SOURCE                   79
#define C5_ETS_AES_INTR_SOURCE                   78
#define C5_ETS_RSA_INTR_SOURCE                   80
#define C5_ETS_ECC_INTR_SOURCE                   81
#define C5_ETS_ECDSA_INTR_SOURCE                 82

#define ESP32C5_INT_MATRIX_INPUTS               84

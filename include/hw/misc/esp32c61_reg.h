/*
 * ESP32-C61 register base addresses and SoC definitions
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
#define DR_REG_SPI0_BASE                        0x60002000
#define DR_REG_SPI1_BASE                        0x60003000
#define DR_REG_UART2_BASE                       0x60006000
#define DR_REG_TIMERGROUP0_BASE                 0x60008000
#define DR_REG_TIMERGROUP1_BASE                 0x60009000
#define DR_REG_SYSTIMER_BASE                    0x6000A000
#define DR_REG_USB_SERIAL_JTAG_BASE             0x6000F000
#define DR_REG_INTERRUPT_BASE                   0x60010000

/* Peripheral 1 register base addresses */
#define DR_REG_GDMA_BASE                        0x60080000
#define DR_REG_SHA_BASE                         0x60089000
#define DR_REG_ECC_MULT_BASE                    0x6008B000
#define DR_REG_ECDSA_BASE                       0x6008E000

/* HP Top peripherals */
#define DR_REG_IO_MUX_BASE                      0x60090000
#define DR_REG_GPIO_BASE                        0x60091000
#define DR_REG_SYSTEM_BASE                      0x60095000
#define DR_REG_SYSCON_BASE                      0x60096000

/* LP System peripherals */
#define DR_REG_EFUSE_BASE                       0x600B4800

/* CPU peripherals */
#define DR_REG_EXTMEM_BASE                      0x600C8000
#define DR_REG_INTPRI_BASE                      0x600C5000

/* CLIC interrupt controller */
#define DR_REG_CLIC_BASE                        0x20800000
#define DR_REG_CLIC_CTRL_BASE                   0x20801000

/* Emulated ethernet (OpenCores) */
#define DR_REG_EMAC_BASE                        0x600CD000

/* SoC constants */
#define ESP32C61_IO_START_ADDR                  (0x60000000)
#define ESP32C61_UART_COUNT                     3

/*
 * ESP32-C61 interrupt source numbers
 * From IDF: components/soc/esp32c61/include/soc/interrupts.h
 */
#define C61_ETS_EFUSE_INTR_SOURCE               14
#define C61_ETS_FROM_CPU_INTR0_SOURCE           19
#define C61_ETS_GPIO_INTR_SOURCE                27
#define C61_ETS_UART0_INTR_SOURCE               40
#define C61_ETS_UART1_INTR_SOURCE               41
#define C61_ETS_UART2_INTR_SOURCE               42
#define C61_ETS_USB_SERIAL_JTAG_INTR_SOURCE     44
#define C61_ETS_TG0_T0_LEVEL_INTR_SOURCE        46
#define C61_ETS_TG0_WDT_LEVEL_INTR_SOURCE       48
#define C61_ETS_TG1_T0_LEVEL_INTR_SOURCE        49
#define C61_ETS_TG1_WDT_LEVEL_INTR_SOURCE       51
#define C61_ETS_SYSTIMER_TARGET0_INTR_SOURCE     52
#define C61_ETS_SYSTIMER_TARGET1_INTR_SOURCE     53
#define C61_ETS_SYSTIMER_TARGET2_INTR_SOURCE     54
#define C61_ETS_DMA_IN_CH0_INTR_SOURCE          58
#define C61_ETS_DMA_IN_CH1_INTR_SOURCE          59
#define C61_ETS_DMA_OUT_CH0_INTR_SOURCE         60
#define C61_ETS_DMA_OUT_CH1_INTR_SOURCE         61
#define C61_ETS_SPI2_INTR_SOURCE                62
#define C61_ETS_SHA_INTR_SOURCE                  63
#define C61_ETS_ECC_INTR_SOURCE                  64
#define C61_ETS_ECDSA_INTR_SOURCE                65

#define ESP32C61_INT_MATRIX_INPUTS              66

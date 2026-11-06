/*
 * ESP32-P4 register base addresses and SoC definitions
 *
 * Standalone header — do not include other SOC reg headers.
 * Bases from IDF: components/soc/esp32p4/register/hw_ver3/soc/reg_base.h
 * Interrupt sources from: components/soc/esp32p4/include/soc/interrupts.h
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

/* ---- Memory / CPU subsystem ---- */
#define DR_REG_CACHE_BASE                       0x3FF10000
#define DR_REG_CLIC_BASE                        0x20800000
#define DR_REG_CLIC_CTRL_BASE                   0x20801000
#define DR_REG_CLIC_CORE1_BASE                  0x20810000

/* ---- HP peripheral window (0x50000000) ---- */
#define DR_REG_GDMA_BASE                        0x50081000
#define DR_REG_AHB_DMA_BASE                     0x50085000
#define DR_REG_AXI_DMA_BASE                     0x5008A000
#define DR_REG_FLASH_SPI0_BASE                  0x5008C000
#define DR_REG_FLASH_SPI1_BASE                  0x5008D000
#define DR_REG_SPI1_BASE                        DR_REG_FLASH_SPI1_BASE
#define DR_REG_PSRAM_MSPI0_BASE                 0x5008E000
#define DR_REG_PSRAM_MSPI1_BASE                 0x5008F000
#define DR_REG_AES_BASE                         0x50090000
#define DR_REG_SHA_BASE                         0x50091000
#define DR_REG_ECC_MULT_BASE                    0x50093000
#define DR_REG_ECDSA_BASE                       0x50096000
#define DR_REG_RSA_BASE                         0x50092000
#define DR_REG_EMAC_BASE                        0x50098000

#define DR_REG_TIMERGROUP0_BASE                 0x500C2000
#define DR_REG_TIMERGROUP1_BASE                 0x500C3000
#define DR_REG_UART_BASE                        0x500CA000
#define DR_REG_UART0_BASE                       DR_REG_UART_BASE
#define DR_REG_UART1_BASE                       0x500CB000
#define DR_REG_UART2_BASE                       0x500CC000
#define DR_REG_UART3_BASE                       0x500CD000
#define DR_REG_UART4_BASE                       0x500CE000
#define DR_REG_SPI2_BASE                        0x500D0000
#define DR_REG_SPI3_BASE                        0x500D1000
#define DR_REG_USB_SERIAL_JTAG_BASE             0x500D2000
#define DR_REG_EMAC_BASE                        0x50098000
#define DR_REG_INTERRUPT_BASE                   0x500D6000
#define DR_REG_INTERRUPT_CORE0_BASE             DR_REG_INTERRUPT_BASE
#define DR_REG_INTERRUPT_CORE1_BASE             0x500D6800
#define DR_REG_INTMTX_BASE                      DR_REG_INTERRUPT_BASE

#define DR_REG_GPIO_BASE                        0x500E0000
#define DR_REG_IO_MUX_BASE                      0x500E1000
#define DR_REG_SYSTIMER_BASE                    0x500E2000
#define DR_REG_HP_SYS_BASE                      0x500E5000
#define DR_REG_HP_SYS_CLKRST_BASE               0x500E6000
#define DR_REG_HP_CLKRST_BASE                   DR_REG_HP_SYS_CLKRST_BASE

/* ---- LP AON / LP peri ---- */
#define DR_REG_LP_SYS_BASE                      0x50110000
#define DR_REG_LP_CLKRST_BASE                   0x50111000
#define DR_REG_LP_TIMER_BASE                    0x50112000
#define DR_REG_PMU_BASE                         0x50115000
#define DR_REG_LP_TRNG_BASE                     0x50126000
#define DR_REG_EFUSE_BASE                       0x5012D000
#define DR_REG_LP_UART_BASE                     0x50121000

/* SoC constants */
#define ESP32P4_IO_START_ADDR                   0x50000000
#define ESP32P4_CPU_COUNT                       2
#define ESP32P4_HP_UART_COUNT                   5
#define ESP32P4_UART_COUNT                      ESP32P4_HP_UART_COUNT
#define ESP32P4_INT_MATRIX_INPUTS               136
/*
 * The CLIC reserves lines 0..15 for core-local interrupts and maps the
 * interrupt matrix outputs above them, so IDF interrupt number N lands on CLIC
 * line N + 16 and the matrix must be able to drive all 48 lines.
 */
#define ESP32P4_CPU_INT_COUNT                   47
#define ESP32P4_CLIC_NUM_INT                    48
#define ESP32P4_XTAL_FREQ_HZ                    40000000

/*
 * Interrupt source numbers
 * From IDF: components/soc/esp32p4/include/soc/interrupts.h. These are enum
 * ordinals, so they must be counted with the ETS_TEMPERATURE_SENSOR alias
 * (which does not advance) and the explicit ETS_LP_UART = 16 in mind.
 */
#define P4_ETS_LP_EFUSE_INTR_SOURCE             17
#define P4_ETS_USB_SERIAL_JTAG_INTR_SOURCE      22
#define P4_ETS_SPI2_INTR_SOURCE                 25
#define P4_ETS_UART0_INTR_SOURCE                31
#define P4_ETS_UART1_INTR_SOURCE                32
#define P4_ETS_UART2_INTR_SOURCE                33
#define P4_ETS_UART3_INTR_SOURCE                34
#define P4_ETS_UART4_INTR_SOURCE                35
#define P4_ETS_TG0_T0_INTR_SOURCE               46
#define P4_ETS_TG0_T1_INTR_SOURCE               47
#define P4_ETS_TG0_WDT_LEVEL_INTR_SOURCE        48
#define P4_ETS_TG1_T0_INTR_SOURCE               49
#define P4_ETS_TG1_T1_INTR_SOURCE               50
#define P4_ETS_TG1_WDT_LEVEL_INTR_SOURCE        51
#define P4_ETS_SYSTIMER_TARGET0_INTR_SOURCE     53
#define P4_ETS_SYSTIMER_TARGET1_INTR_SOURCE     54
#define P4_ETS_SYSTIMER_TARGET2_INTR_SOURCE     55
#define P4_ETS_AHB_PDMA_IN_CH0_INTR_SOURCE      56
#define P4_ETS_AHB_PDMA_IN_CH1_INTR_SOURCE      57
#define P4_ETS_AHB_PDMA_IN_CH2_INTR_SOURCE      58
#define P4_ETS_AHB_PDMA_OUT_CH0_INTR_SOURCE     59
#define P4_ETS_AHB_PDMA_OUT_CH1_INTR_SOURCE     60
#define P4_ETS_AHB_PDMA_OUT_CH2_INTR_SOURCE     61
#define P4_ETS_SHA_INTR_SOURCE                  70
#define P4_ETS_AXI_PDMA_IN_CH0_INTR_SOURCE      62
#define P4_ETS_AXI_PDMA_OUT_CH0_INTR_SOURCE     65
#define P4_ETS_RSA_INTR_SOURCE                   68
#define P4_ETS_AES_INTR_SOURCE                   69
#define P4_ETS_ECC_INTR_SOURCE                   71
#define P4_ETS_ECDSA_INTR_SOURCE                 72
#define P4_ETS_ETH_MAC_INTR_SOURCE              92
#define P4_ETS_GPIO_INTR0_SOURCE                74
#define P4_ETS_FROM_CPU_INTR0_SOURCE            79
#define P4_ETS_FROM_CPU_INTR1_SOURCE            80
#define P4_ETS_FROM_CPU_INTR2_SOURCE            81
#define P4_ETS_FROM_CPU_INTR3_SOURCE            82

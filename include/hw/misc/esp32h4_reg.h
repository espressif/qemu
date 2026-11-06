/*
 * ESP32-H4 register base addresses and SoC definitions
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

/* CPU / debug */
#define DR_REG_CLINT_M_BASE                     0x20000000
#define DR_REG_CLIC_BASE                        0x20800000
#define DR_REG_CLIC_CTRL_BASE                   0x20801000
/* Other hart's CLIC, reached as CLIC + DUALCORE_CLIC_CTRL_OFF (0x10000). */
#define DR_REG_CLIC_CORE1_BASE                  0x20810000

/* Early peripherals */
#define DR_REG_INTPRI_BASE                      0x60005000
#define DR_REG_CACHE_BASE                       0x60008000
#define DR_REG_EXTMEM_BASE                      DR_REG_CACHE_BASE

#define DR_REG_GPSPI2_BASE                      0x60010000
#define DR_REG_GPSPI3_BASE                      0x60011000
#define DR_REG_UART_BASE                        0x60012000
#define DR_REG_UART0_BASE                       DR_REG_UART_BASE
#define DR_REG_UART1_BASE                       0x60013000
#define DR_REG_USB_SERIAL_JTAG_BASE             0x6001D000
#define DR_REG_AHB_DMA_BASE                     0x6001F000
#define DR_REG_GDMA_BASE                        DR_REG_AHB_DMA_BASE
#define DR_REG_SYSTIMER_BASE                    0x60023000

#define DR_REG_TIMERGROUP0_BASE                 0x60090000
#define DR_REG_TIMERGROUP1_BASE                 0x60091000
#define DR_REG_IO_MUX_BASE                      0x60092000
#define DR_REG_GPIO_BASE                        0x60093000
#define DR_REG_PCR_BASE                         0x60094000
#define DR_REG_SYSCON_BASE                      DR_REG_PCR_BASE
#define DR_REG_SPIMEM0_BASE                     0x60098000
#define DR_REG_SPIMEM1_BASE                     0x60099000
#define DR_REG_SPI0_BASE                        DR_REG_SPIMEM0_BASE
#define DR_REG_SPI1_BASE                        DR_REG_SPIMEM1_BASE
#define DR_REG_INTMTX0_BASE                     0x6009A000
#define DR_REG_INTMTX1_BASE                     0x6009B000
#define DR_REG_INTERRUPT_BASE                   DR_REG_INTMTX0_BASE
#define DR_REG_INTERRUPT_CORE1_BASE             DR_REG_INTMTX1_BASE
#define DR_REG_HP_SYSTEM_BASE                   0x6009C000
#define DR_REG_SYSTEM_BASE                      DR_REG_HP_SYSTEM_BASE
#define DR_REG_AES_BASE                         0x600A6000
#define DR_REG_SHA_BASE                         0x600A7000
#define DR_REG_ECC_MULT_BASE                    0x600A8000
#define DR_REG_ECDSA_BASE                       0x600AA000

/* LP / AON */
#define DR_REG_EFUSE_BASE                       0x600B1800
#define DR_REG_TRNG_BASE                        0x600B2000
#define DR_REG_RNG_BASE                         DR_REG_TRNG_BASE
#define DR_REG_LP_AON_BASE                      0x600B2800
#define DR_REG_LP_CLKRST_BASE                   0x600B3000
#define DR_REG_LPPERI_BASE                      0x600B3400
#define DR_REG_LP_TIMER_BASE                    0x600B5000
#define DR_REG_I2C_ANA_MST_BASE                 0x600CF800

/* Emulated ethernet (OpenCores) — unused H4 address */
#define DR_REG_EMAC_BASE                        0x600D0000

/* SoC constants */
#define ESP32H4_IO_START_ADDR                   (0x60000000)
#define ESP32H4_CPU_COUNT                       2
#define ESP32H4_UART_COUNT                      2
#define ESP32H4_INT_MATRIX_INPUTS               98
#define ESP32H4_CPU_INT_COUNT                   47
#define ESP32H4_CLIC_NUM_INT                    48

/*
 * Interrupt source numbers from IDF soc/esp32h4/include/soc/interrupts.h
 * Count carefully: ETS_TEMPERATURE_SENSOR aliases ETS_APB_ADC (=71).
 */
#define H4_ETS_EFUSE_INTR_SOURCE                16
#define H4_ETS_FROM_CPU_INTR0_SOURCE            23
#define H4_ETS_GPIO_INTR_SOURCE                 32
#define H4_ETS_UART0_INTR_SOURCE                55
#define H4_ETS_UART1_INTR_SOURCE                56
#define H4_ETS_USB_SERIAL_JTAG_INTR_SOURCE      60
#define H4_ETS_TG0_T0_LEVEL_INTR_SOURCE         64
#define H4_ETS_TG0_WDT_LEVEL_INTR_SOURCE        65
#define H4_ETS_TG1_T0_LEVEL_INTR_SOURCE         66
#define H4_ETS_TG1_WDT_LEVEL_INTR_SOURCE        67
#define H4_ETS_SYSTIMER_TARGET0_INTR_SOURCE     68
#define H4_ETS_SYSTIMER_TARGET1_INTR_SOURCE     69
#define H4_ETS_SYSTIMER_TARGET2_INTR_SOURCE     70
#define H4_ETS_DMA_IN_CH0_INTR_SOURCE           81
#define H4_ETS_DMA_IN_CH1_INTR_SOURCE           82
#define H4_ETS_DMA_IN_CH2_INTR_SOURCE           83
#define H4_ETS_DMA_IN_CH3_INTR_SOURCE           84
#define H4_ETS_DMA_IN_CH4_INTR_SOURCE           85
#define H4_ETS_DMA_OUT_CH0_INTR_SOURCE          86
#define H4_ETS_DMA_OUT_CH1_INTR_SOURCE          87
#define H4_ETS_DMA_OUT_CH2_INTR_SOURCE          88
#define H4_ETS_DMA_OUT_CH3_INTR_SOURCE          89
#define H4_ETS_DMA_OUT_CH4_INTR_SOURCE          90
#define H4_ETS_SHA_INTR_SOURCE                  94
#define H4_ETS_AES_INTR_SOURCE                   93
#define H4_ETS_ECC_INTR_SOURCE                   95
#define H4_ETS_ECDSA_INTR_SOURCE                 96

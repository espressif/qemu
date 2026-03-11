/*
 * ESP32-H2 register base addresses and SoC definitions
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

/* Peripheral register base addresses (from ESP-IDF soc/esp32h2/register/soc/reg_base.h) */
#define DR_REG_UART_BASE                        0x60000000
#define DR_REG_UART1_BASE                       0x60001000
#define DR_REG_SPI1_BASE                        0x60003000
#define DR_REG_TIMERGROUP0_BASE                 0x60009000
#define DR_REG_TIMERGROUP1_BASE                 0x6000A000
#define DR_REG_SYSTIMER_BASE                    0x6000B000
#define DR_REG_TWAI_BASE                        0x6000C000
#define DR_REG_USB_SERIAL_JTAG_BASE             0x6000F000
#define DR_REG_INTERRUPT_BASE                   0x60010000
#define DR_REG_GDMA_BASE                        0x60080000
#define DR_REG_AES_BASE                         0x60088000
#define DR_REG_SHA_BASE                         0x60089000
#define DR_REG_RSA_BASE                         0x6008A000
#define DR_REG_HMAC_BASE                        0x6008D000
#define DR_REG_DIGITAL_SIGNATURE_BASE           0x6008C000
#define DR_REG_GPIO_BASE                        0x60091000
#define DR_REG_SYSTEM_BASE                      0x60095000
#define DR_REG_SYSCON_BASE                      0x60096000
#define DR_REG_EFUSE_BASE                       0x600B0800
#define DR_REG_RTCCNTL_BASE                     0x600B1C00
#define DR_REG_ASSIST_DEBUG_BASE                0x600C2000
#define DR_REG_EXTMEM_BASE                      0x600C8000

#define DR_REG_INTPRI_BASE                      0x600C5000
#define DR_REG_PLIC_MX_BASE                     0x20001000
#define DR_REG_PLIC_UX_BASE                     0x20001400

#define ESP32H2_IO_START_ADDR                   (0x60000000)
#define ESP32H2_UART_COUNT                      2

/* ESP32-H2 interrupt source numbers (from IDF soc/esp32h2/include/soc/interrupts.h) */
#define H2_ETS_EFUSE_INTR_SOURCE                1
#define H2_ETS_FROM_CPU_INTR0_SOURCE            7
#define H2_ETS_GPIO_INTR_SOURCE                 22
#define H2_ETS_UART0_INTR_SOURCE                33
#define H2_ETS_UART1_INTR_SOURCE                34
#define H2_ETS_USB_SERIAL_JTAG_INTR_SOURCE      37
#define H2_ETS_TG0_T0_LEVEL_INTR_SOURCE         41
#define H2_ETS_TG0_WDT_LEVEL_INTR_SOURCE        42
#define H2_ETS_TG1_T0_LEVEL_INTR_SOURCE         43
#define H2_ETS_TG1_WDT_LEVEL_INTR_SOURCE        44
#define H2_ETS_SYSTIMER_TARGET0_INTR_SOURCE      45
#define H2_ETS_SYSTIMER_TARGET1_INTR_SOURCE      46
#define H2_ETS_SYSTIMER_TARGET2_INTR_SOURCE      47
#define H2_ETS_DMA_IN_CH0_INTR_SOURCE           53
#define H2_ETS_DMA_IN_CH1_INTR_SOURCE           54
#define H2_ETS_DMA_IN_CH2_INTR_SOURCE           55
#define H2_ETS_DMA_OUT_CH0_INTR_SOURCE          56
#define H2_ETS_DMA_OUT_CH1_INTR_SOURCE          57
#define H2_ETS_DMA_OUT_CH2_INTR_SOURCE          58
#define H2_ETS_SPI2_INTR_SOURCE                 59
#define H2_ETS_SHA_INTR_SOURCE                   61

#define ESP32H2_INT_MATRIX_INPUTS               65

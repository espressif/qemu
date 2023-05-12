/*
 * ESP32-C3 Interrupt Matrix
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/riscv/riscv_hart.h"
#include "target/riscv/esp_cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32c3_reg.h"


/**
 * Number of inputs in the C3's Interrupt Matrix
 */
#define ESP32C3_INT_MATRIX_INPUTS   62

/**
 * Name of the output lines of the Interrupt Matrix (that shall be connected to the CPU)
 */
#define ESP32C3_INT_MATRIX_OUTPUT_NAME "misc.esp32c3.intmatrix.out_irqs"

/**
 * Number of CPU peripheral interrupts on the C3.
 * This can be considered the output of the interrupt matrix.
 */
#define ESP32C3_CPU_INT_COUNT       (ESP_CPU_INT_LINES)

#define TYPE_ESP32C3_INTMATRIX "misc.esp32c3.intmatrix"
#define ESP32C3_INTMATRIX(obj) OBJECT_CHECK(ESP32C3IntMatrixState, (obj), TYPE_ESP32C3_INTMATRIX)

/**
 * Size of the I/O region, in bytes, of the C3 Interrupt Matrix
 */
#define ESP32C3_INTMATRIX_IO_SIZE (0x800)


/**
 * Index where priority registers start
 */
#define ESP32C3_INTMATRIX_IO_PRIO_START (0x118 / sizeof(uint32_t))

#define ESP32C3_INTMATRIX_IO_PRIO_END   (0x190 / sizeof(uint32_t))

/**
 * The following registers are not part of any "register table", contrarily
 * to the priorities or mapping.
 */
#define ESP32C3_INTMATRIX_IO_ENABLE_REG (0x104 / sizeof(uint32_t))

#define ESP32C3_INTMATRIX_IO_TYPE_REG   (0x108 / sizeof(uint32_t))

#define ESP32C3_INTMATRIX_IO_THRESH_REG (0x194 / sizeof(uint32_t))


/* Bit value for the type of interrupt trigger  */
#define ESP322C3_INTMATRIX_TRIG_LEVEL   0
#define ESP322C3_INTMATRIX_TRIG_EDGE    1

typedef struct ESP32C3IntMatrixState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t irq_map[ESP32C3_INT_MATRIX_INPUTS];
    /* In the following fields, "interrupts" refer to the CPU lines (31)
     * and not the peripheral source. */
    /* ESP32-C3 CPU has 31 interrupts numbered from 1 to 31 */
    uint8_t irq_prio[ESP32C3_CPU_INT_COUNT + 1];
    /* Current priority threshold of the CPU interrupts */
    uint8_t irq_thres;
    /* Keep a bitmap of the pending interrupts */
    uint64_t irq_pending;
    /* Bitmap that records the enabled/disabled interrupts */
    uint64_t irq_enabled;
    /* Bitmap that records the type of trigger for interrupts */
    uint64_t irq_trigger;

    /* Fast mirror to access IRQ levels */
    uint64_t irq_levels;
    EspRISCVCPU *cpu;

    /* Output IRQ used to notify the CPU, indexed from 1 to 31, so allocate one more */
    qemu_irq out_irqs[ESP32C3_CPU_INT_COUNT + 1];
} ESP32C3IntMatrixState;

_Static_assert(sizeof(uint64_t) * 8 >= ESP32C3_INT_MATRIX_INPUTS,
               "A single 64-bit value must be able to represent a bitmap of all the interrupt sources");

typedef enum {
    ETS_WIFI_MAC_INTR_SOURCE = 0,               /**< interrupt of WiFi MAC, level*/
    ETS_WIFI_MAC_NMI_SOURCE,                    /**< interrupt of WiFi MAC, NMI, use if MAC have bug to fix in NMI*/
    ETS_WIFI_PWR_INTR_SOURCE,                   /**< */
    ETS_WIFI_BB_INTR_SOURCE,                    /**< interrupt of WiFi BB, level, we can do some calibartion*/
    ETS_BT_MAC_INTR_SOURCE,                     /**< will be cancelled*/
    ETS_BT_BB_INTR_SOURCE,                      /**< interrupt of BT BB, level*/
    ETS_BT_BB_NMI_SOURCE,                       /**< interrupt of BT BB, NMI, use if BB have bug to fix in NMI*/
    ETS_RWBT_INTR_SOURCE,                       /**< interrupt of RWBT, level*/
    ETS_RWBLE_INTR_SOURCE,                      /**< interrupt of RWBLE, level*/
    ETS_RWBT_NMI_SOURCE,                        /**< interrupt of RWBT, NMI, use if RWBT have bug to fix in NMI*/
    ETS_RWBLE_NMI_SOURCE,                       /**< interrupt of RWBLE, NMI, use if RWBT have bug to fix in NMI*/
    ETS_I2C_MASTER_SOURCE,                      /**< interrupt of I2C Master, level*/
    ETS_SLC0_INTR_SOURCE,                       /**< interrupt of SLC0, level*/
    ETS_SLC1_INTR_SOURCE,                       /**< interrupt of SLC1, level*/
    ETS_APB_CTRL_INTR_SOURCE,                   /**< interrupt of APB ctrl, ?*/
    ETS_UHCI0_INTR_SOURCE,                      /**< interrupt of UHCI0, level*/
    ETS_GPIO_INTR_SOURCE,                       /**< interrupt of GPIO, level*/
    ETS_GPIO_NMI_SOURCE,                        /**< interrupt of GPIO, NMI*/
    ETS_SPI1_INTR_SOURCE,                       /**< interrupt of SPI1, level, SPI1 is for flash read/write, do not use this*/
    ETS_SPI2_INTR_SOURCE,                       /**< interrupt of SPI2, level*/
    ETS_I2S1_INTR_SOURCE,                       /**< interrupt of I2S1, level*/
    ETS_UART0_INTR_SOURCE,                      /**< interrupt of UART0, level*/
    ETS_UART1_INTR_SOURCE,                      /**< interrupt of UART1, level*/
    ETS_LEDC_INTR_SOURCE,                       /**< interrupt of LED PWM, level*/
    ETS_EFUSE_INTR_SOURCE,                      /**< interrupt of efuse, level, not likely to use*/
    ETS_TWAI_INTR_SOURCE,                       /**< interrupt of can, level*/
    ETS_USB_SERIAL_JTAG_INTR_SOURCE,            /**< interrupt of USJ, level*/
    ETS_RTC_CORE_INTR_SOURCE,                   /**< interrupt of rtc core, level, include rtc watchdog*/
    ETS_RMT_INTR_SOURCE,                        /**< interrupt of remote controller, level*/
    ETS_I2C_EXT0_INTR_SOURCE,                   /**< interrupt of I2C controller1, level*/
    ETS_TIMER1_INTR_SOURCE,
    ETS_TIMER2_INTR_SOURCE,
    ETS_TG0_T0_LEVEL_INTR_SOURCE,               /**< interrupt of TIMER_GROUP0, TIMER0, level*/
    ETS_TG0_WDT_LEVEL_INTR_SOURCE,              /**< interrupt of TIMER_GROUP0, WATCH DOG, level*/
    ETS_TG1_T0_LEVEL_INTR_SOURCE,               /**< interrupt of TIMER_GROUP1, TIMER0, level*/
    ETS_TG1_WDT_LEVEL_INTR_SOURCE,              /**< interrupt of TIMER_GROUP1, WATCHDOG, level*/
    ETS_CACHE_IA_INTR_SOURCE,                   /**< interrupt of Cache Invalid Access, LEVEL*/
    ETS_SYSTIMER_TARGET0_EDGE_INTR_SOURCE,      /**< interrupt of system timer 0, EDGE*/
    ETS_SYSTIMER_TARGET1_EDGE_INTR_SOURCE,      /**< interrupt of system timer 1, EDGE*/
    ETS_SYSTIMER_TARGET2_EDGE_INTR_SOURCE,      /**< interrupt of system timer 2, EDGE*/
    ETS_SPI_MEM_REJECT_CACHE_INTR_SOURCE,       /**< interrupt of SPI0 Cache access and SPI1 access rejected, LEVEL*/
    ETS_ICACHE_PRELOAD0_INTR_SOURCE,            /**< interrupt of ICache per load operation, LEVEL*/
    ETS_ICACHE_SYNC0_INTR_SOURCE,               /**< interrupt of instruction cache sync done, LEVEL*/
    ETS_APB_ADC_INTR_SOURCE,                    /**< interrupt of APB ADC, LEVEL*/
    ETS_DMA_CH0_INTR_SOURCE,                    /**< interrupt of general DMA channel 0, LEVEL*/
    ETS_DMA_CH1_INTR_SOURCE,                    /**< interrupt of general DMA channel 1, LEVEL*/
    ETS_DMA_CH2_INTR_SOURCE,                    /**< interrupt of general DMA channel 2, LEVEL*/
    ETS_RSA_INTR_SOURCE,                        /**< interrupt of RSA accelerator, level*/
    ETS_AES_INTR_SOURCE,                        /**< interrupt of AES accelerator, level*/
    ETS_SHA_INTR_SOURCE,                        /**< interrupt of SHA accelerator, level*/
    ETS_FROM_CPU_INTR0_SOURCE,                  /**< interrupt0 generated from a CPU, level*/ /* Used for FreeRTOS */
    ETS_FROM_CPU_INTR1_SOURCE,                  /**< interrupt1 generated from a CPU, level*/ /* Used for FreeRTOS */
    ETS_FROM_CPU_INTR2_SOURCE,                  /**< interrupt2 generated from a CPU, level*/
    ETS_FROM_CPU_INTR3_SOURCE,                  /**< interrupt3 generated from a CPU, level*/
    ETS_ASSIST_DEBUG_INTR_SOURCE,               /**< interrupt of Assist debug module, LEVEL*/
    ETS_DMA_APBPERI_PMS_INTR_SOURCE,
    ETS_CORE0_IRAM0_PMS_INTR_SOURCE,
    ETS_CORE0_DRAM0_PMS_INTR_SOURCE,
    ETS_CORE0_PIF_PMS_INTR_SOURCE,
    ETS_CORE0_PIF_PMS_SIZE_INTR_SOURCE,
    ETS_BAK_PMS_VIOLATE_INTR_SOURCE,
    ETS_CACHE_CORE0_ACS_INTR_SOURCE,
    ETS_MAX_INTR_SOURCE,
} periph_interrupt_t;

_Static_assert(ESP32C3_INT_MATRIX_INPUTS == ETS_MAX_INTR_SOURCE,
               "ESP32C3_INT_MATRIX_INPUTS macro doesn't match the number of interrupt sources");

/**
 * @brief Since wifi is not supported on ESP32-C3 target emulation, reuse its interrupt source for ethernet
 */
#define ETS_ETH_MAC_INTR_SOURCE     ETS_WIFI_MAC_INTR_SOURCE

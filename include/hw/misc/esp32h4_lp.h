/*
 * ESP32-H4 LP (Low Power) domain controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32H4_LP "esp32h4.lp"
#define ESP32H4_LP(obj) OBJECT_CHECK(ESP32H4LpState, (obj), TYPE_ESP32H4_LP)

#define ESP32H4_LP_IO_SIZE 0x6000
#define ESP32H4_LP_BASE     0x600B0000
#define ESP32H4_LP_RESET_GPIO "esp32h4-lp-reset"

/* Raised while LP_AON_CPUCORE_CFG.CPU_CORE1_SW_STALL holds 0x86. */
#define ESP32H4_LP_CORE1_STALL_GPIO "esp32h4.lp.core1-stall"
/* Pulsed when the guest writes LP_AON_CPUCORE_CFG.CPU_CORE1_SW_RESET. */
#define ESP32H4_LP_CORE1_SW_RESET_GPIO "esp32h4.lp.core1-sw-reset"

typedef enum ESP32H4ResetReason {
    ESP32H4_CHIP_POWER_ON        = 0x01,
    ESP32H4_CORE_SW              = 0x03,
    ESP32H4_CORE_DEEP_SLEEP      = 0x05,
    ESP32H4_CORE_MWDT0           = 0x07,
    ESP32H4_CORE_MWDT1           = 0x08,
    ESP32H4_CORE_RTC_WDT         = 0x09,
    ESP32H4_CPU0_MWDT0           = 0x0B,
    ESP32H4_CPU0_SW              = 0x0C,
    ESP32H4_CPU0_RTC_WDT         = 0x0D,
    ESP32H4_SYS_BROWN_OUT        = 0x0F,
    ESP32H4_SYS_RTC_WDT          = 0x10,
    ESP32H4_CPU0_MWDT1           = 0x11,
    ESP32H4_SYS_SUPER_WDT        = 0x12,
    ESP32H4_CORE_PWR_GLITCH      = 0x13,
    ESP32H4_CORE_EFUSE_CRC       = 0x14,
    ESP32H4_CORE_USB_UART        = 0x15,
    ESP32H4_CORE_USB_JTAG        = 0x16,
    ESP32H4_CPU0_JTAG            = 0x18,
    ESP32H4_CPU_LOCKUP           = 0x1A,
    ESP32H4_RESET_REASON_MAX     = 0x1B,
} ESP32H4ResetReason;

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint64_t lp_timer_counter;
    ESP32H4ResetReason reset_reason;
    qemu_irq cpu_reset;
    /* LP_AON_CPUCORE_CFG. Reset value keeps both cores' STAT_VECTOR_SEL on ROM. */
    uint32_t cpucore_cfg;
    qemu_irq core1_stall;
    qemu_irq core1_sw_reset;
} ESP32H4LpState;

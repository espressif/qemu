/*
 * ESP32-H2 LP (Low Power) domain controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32H2_LP "esp32h2.lp"
#define ESP32H2_LP(obj) OBJECT_CHECK(ESP32H2LpState, (obj), TYPE_ESP32H2_LP)

#define ESP32H2_LP_IO_SIZE 0x4000
#define ESP32H2_LP_BASE     0x600B0000
#define ESP32H2_LP_RESET_GPIO "esp32h2-lp-reset"

typedef enum ESP32H2ResetReason {
    ESP32H2_CHIP_POWER_ON        = 0x01,
    ESP32H2_CORE_SW              = 0x03,
    ESP32H2_CORE_DEEP_SLEEP      = 0x05,
    ESP32H2_CORE_MWDT0           = 0x07,
    ESP32H2_CORE_MWDT1           = 0x08,
    ESP32H2_CORE_RTC_WDT         = 0x09,
    ESP32H2_CPU0_MWDT0           = 0x0B,
    ESP32H2_CPU0_SW              = 0x0C,
    ESP32H2_CPU0_RTC_WDT         = 0x0D,
    ESP32H2_SYS_BROWN_OUT        = 0x0F,
    ESP32H2_SYS_RTC_WDT          = 0x10,
    ESP32H2_CPU0_MWDT1           = 0x11,
    ESP32H2_SYS_SUPER_WDT        = 0x12,
    ESP32H2_CORE_EFUSE_CRC       = 0x14,
    ESP32H2_CORE_USB_UART        = 0x15,
    ESP32H2_CORE_USB_JTAG        = 0x16,
    ESP32H2_CORE_PWR_GLITCH      = 0x17,
    ESP32H2_CPU0_JTAG            = 0x18,
    ESP32H2_RESET_REASON_MAX     = 0x19,
} ESP32H2ResetReason;

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint64_t lp_timer_counter;
    ESP32H2ResetReason reset_reason;
    qemu_irq cpu_reset;
} ESP32H2LpState;

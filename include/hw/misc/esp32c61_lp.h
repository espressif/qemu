/*
 * ESP32-C61 LP (Low Power) domain controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32C61_LP "esp32c61.lp"
#define ESP32C61_LP(obj) OBJECT_CHECK(ESP32C61LpState, (obj), TYPE_ESP32C61_LP)

#define ESP32C61_LP_IO_SIZE 0x5000
#define ESP32C61_LP_BASE     0x600B0000
#define ESP32C61_LP_RESET_GPIO "esp32c61-lp-reset"

typedef enum ESP32C61ResetReason {
    ESP32C61_CHIP_POWER_ON        = 0x01,
    ESP32C61_CORE_SW              = 0x03,
    ESP32C61_CORE_DEEP_SLEEP      = 0x05,
    ESP32C61_CORE_MWDT0           = 0x07,
    ESP32C61_CORE_MWDT1           = 0x08,
    ESP32C61_CORE_RTC_WDT         = 0x09,
    ESP32C61_CPU0_MWDT0           = 0x0B,
    ESP32C61_CPU0_SW              = 0x0C,
    ESP32C61_CPU0_RTC_WDT         = 0x0D,
    ESP32C61_SYS_BROWN_OUT        = 0x0F,
    ESP32C61_SYS_RTC_WDT          = 0x10,
    ESP32C61_CPU0_MWDT1           = 0x11,
    ESP32C61_SYS_SUPER_WDT        = 0x12,
    ESP32C61_CORE_EFUSE_CRC       = 0x14,
    ESP32C61_CORE_USB_UART        = 0x15,
    ESP32C61_CORE_USB_JTAG        = 0x16,
    ESP32C61_CPU0_JTAG            = 0x18,
    ESP32C61_RTC_BROWN_OUT        = 0x19,
    ESP32C61_CPU0_LOCKUP          = 0x1A,
    ESP32C61_RESET_REASON_MAX     = 0x1B,
} ESP32C61ResetReason;

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint64_t lp_timer_counter;
    /* LP_AON_STOREn, retained across software resets */
    uint32_t aon_store[10];
    ESP32C61ResetReason reset_reason;
    qemu_irq cpu_reset;
} ESP32C61LpState;

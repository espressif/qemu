/*
 * ESP32-P4 LP (Low Power) domain controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32P4_LP "esp32p4.lp"
#define ESP32P4_LP(obj) OBJECT_CHECK(ESP32P4LpState, (obj), TYPE_ESP32P4_LP)

/* Covers 0x50110000-0x5012FFFF relative */
#define ESP32P4_LP_IO_SIZE 0x20000
#define ESP32P4_LP_BASE     0x50110000
#define ESP32P4_LP_RESET_GPIO "esp32p4-lp-reset"

/* LP_PERI_CLKRST (LPPERI) block at 0x50120000 */
#define ESP32P4_LPPERI_OFFSET 0x10000
#define ESP32P4_LPPERI_SIZE   0x400

/* Reset reasons matching IDF soc/reset_reasons.h for ESP32-P4 */
typedef enum ESP32P4ResetReason {
    ESP32P4_CHIP_POWER_ON        = 0x01,
    ESP32P4_CORE_SW              = 0x03,
    ESP32P4_CORE_DEEP_SLEEP      = 0x05,
    ESP32P4_CORE_MWDT            = 0x07,
    ESP32P4_CORE_RWDT            = 0x09,
    ESP32P4_CPU_MWDT             = 0x0B,
    ESP32P4_CPU_SW               = 0x0C,
    ESP32P4_CPU_RWDT             = 0x0D,
    ESP32P4_SYS_BROWN_OUT        = 0x0F,
    ESP32P4_SYS_RWDT             = 0x10,
    ESP32P4_SYS_SUPER_WDT        = 0x12,
    ESP32P4_CORE_PWR_GLITCH      = 0x13,
    ESP32P4_CORE_EFUSE_CRC       = 0x14,
    ESP32P4_CORE_USB_JTAG        = 0x16,
    ESP32P4_CORE_USB_UART        = 0x17,
    ESP32P4_CPU_JTAG             = 0x18,
    ESP32P4_CPU_LOCKUP           = 0x1A,
    ESP32P4_RESET_REASON_MAX     = 0x1B,
} ESP32P4ResetReason;

/* TIMG wiring convenience aliases (IDF collapses MWDT) */
#define ESP32P4_CORE_MWDT0  ESP32P4_CORE_MWDT
#define ESP32P4_CORE_MWDT1  0x08

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    ESP32P4ResetReason reset_reason;
    qemu_irq cpu_reset;
    uint32_t lpperi[ESP32P4_LPPERI_SIZE / 4];
} ESP32P4LpState;

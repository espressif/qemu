/*
 * ESP32-C6 Interrupt Matrix + PLIC
 *
 * The C6 splits interrupt control across two address blocks:
 *   - Interrupt mapping registers at DR_REG_INTMTX_BASE (0x60010000)
 *   - PLIC enable/priority/threshold at DR_REG_PLIC_MX_BASE (0x20001000)
 *
 * This device exposes both as separate MMIO regions (region 0 = mapping,
 * region 1 = PLIC) so the machine code can place them at the correct
 * addresses.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
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

#define ESP32C6_INT_MATRIX_INPUTS   77
#define ESP32C6_CPU_INT_COUNT       (ESP_CPU_INT_LINES)

#define ESP32C6_INT_MATRIX_OUTPUT_NAME "misc.esp32c6.intmatrix.out_irqs"

#define TYPE_ESP32C6_INTMATRIX "misc.esp32c6.intmatrix"
#define ESP32C6_INTMATRIX(obj) \
    OBJECT_CHECK(ESP32C6IntMatrixState, (obj), TYPE_ESP32C6_INTMATRIX)

#define ESP32C6_INTMATRIX_MAP_IO_SIZE  0x800
#define ESP32C6_PLIC_IO_SIZE           0x400

typedef struct ESP32C6IntMatrixState {
    SysBusDevice parent_obj;

    MemoryRegion map_iomem;
    MemoryRegion plic_iomem;

    uint8_t irq_map[ESP32C6_INT_MATRIX_INPUTS];

    uint8_t irq_prio[ESP32C6_CPU_INT_COUNT + 1];
    uint8_t irq_thres;
    uint64_t irq_enabled;
    uint64_t irq_trigger;

    /* >64 sources, so use a two-element array */
    uint64_t irq_levels[2];

    EspRISCVCPU *cpu;
    qemu_irq out_irqs[ESP32C6_CPU_INT_COUNT + 1];
} ESP32C6IntMatrixState;

_Static_assert(sizeof(uint64_t) * 2 * 8 >= ESP32C6_INT_MATRIX_INPUTS,
               "irq_levels[2] must cover all interrupt sources");

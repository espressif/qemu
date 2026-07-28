/*
 * ESP32-P4 Interrupt Matrix + CLIC
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
#include "hw/misc/esp32p4_reg.h"

#define ESP32P4_INT_MATRIX_OUTPUT_NAME "misc.esp32p4.intmatrix.out_irqs"

#define TYPE_ESP32P4_INTMATRIX "misc.esp32p4.intmatrix"
#define ESP32P4_INTMATRIX(obj) \
    OBJECT_CHECK(ESP32P4IntMatrixState, (obj), TYPE_ESP32P4_INTMATRIX)

#define ESP32P4_INT_MATRIX_MAP_IO_SIZE  0x800
#define ESP32P4_CLIC_IO_SIZE            0x2000

/* 136 sources need 3×64-bit words */
#define ESP32P4_IRQ_LEVELS_WORDS  3

typedef struct ESP32P4IntMatrixState {
    SysBusDevice parent_obj;

    MemoryRegion map_iomem;
    MemoryRegion clic_iomem;

    uint8_t irq_map[ESP32P4_INT_MATRIX_INPUTS];

    uint8_t irq_prio[ESP32P4_CPU_INT_COUNT + 1];
    uint8_t irq_thres;
    uint64_t irq_enabled;
    uint64_t irq_trigger;

    uint64_t irq_levels[ESP32P4_IRQ_LEVELS_WORDS];

    uint32_t clic_config;
    uint32_t clic_ctrl[ESP32P4_CLIC_NUM_INT];

    EspRISCVCPU *cpu;
    qemu_irq out_irqs[ESP32P4_CPU_INT_COUNT + 1];
} ESP32P4IntMatrixState;

_Static_assert(sizeof(uint64_t) * ESP32P4_IRQ_LEVELS_WORDS * 8 >= ESP32P4_INT_MATRIX_INPUTS,
               "irq_levels must cover all interrupt sources");

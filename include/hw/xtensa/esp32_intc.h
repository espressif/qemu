/*
 * ESP32 Interrupt Matrix
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/bitmap.h"
#include "target/xtensa/cpu.h"
#include "target/xtensa/cpu-qom.h"

#define ESP32_CPU_COUNT 2
#define ESP32_INT_MATRIX_INPUTS 69
/* DPORT_{PRO,APP}_INTR_STATUS_{0,1,2}_REG, relative to the DPORT base */
#define ESP32_INT_MATRIX_STATUS_OFFSET 0xEC
#define ESP32_INT_MATRIX_STATUS_WORDS 3

#define TYPE_ESP32_INTMATRIX "misc.esp32.intmatrix"
#define ESP32_INTMATRIX(obj) OBJECT_CHECK(Esp32IntMatrixState, (obj), TYPE_ESP32_INTMATRIX)


typedef struct Esp32IntMatrixState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion status_iomem;
    qemu_irq *outputs[ESP32_CPU_COUNT];
    uint8_t irq_map[ESP32_CPU_COUNT][ESP32_INT_MATRIX_INPUTS];
    /* Current level of every input (source) line */
    DECLARE_BITMAP(irq_levels, ESP32_INT_MATRIX_INPUTS);
    /* Level last driven on each CPU interrupt, one bit per interrupt */
    uint32_t out_levels[ESP32_CPU_COUNT];

    /* properties */
    XtensaCPU *cpu[ESP32_CPU_COUNT];
} Esp32IntMatrixState;


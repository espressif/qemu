#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "target/xtensa/cpu.h"
#include "target/xtensa/cpu-qom.h"

#define TYPE_ESP32_INTMATRIX "misc.esp32.intmatrix"
#define ESP32_INTMATRIX(obj) OBJECT_CHECK(Esp32IntMatrixState, (obj), TYPE_ESP32_INTMATRIX)

typedef struct Esp32IntMatrixState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq *outputs;
    uint8_t irq_map[ESP32_INT_MATRIX_INPUTS];

    /* properties */
    XtensaCPU *cpu;
} Esp32IntMatrixState;

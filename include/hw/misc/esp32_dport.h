#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"
#include "target/xtensa/cpu.h"
#include "target/xtensa/cpu-qom.h"


#define TYPE_ESP32_INTMATRIX "misc.esp32.intmatrix"
#define ESP32_INTMATRIX(obj) OBJECT_CHECK(Esp32IntMatrixState, (obj), TYPE_ESP32_INTMATRIX)

typedef struct Esp32IntMatrixState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq *outputs[ESP32_CPU_COUNT];
    uint8_t irq_map[ESP32_CPU_COUNT][ESP32_INT_MATRIX_INPUTS];

    /* properties */
    XtensaCPU *cpu[ESP32_CPU_COUNT];
} Esp32IntMatrixState;


#define TYPE_ESP32_CROSSCORE_INT "misc.esp32.crosscoreint"
#define ESP32_CROSSCORE_INT(obj) OBJECT_CHECK(Esp32CrosscoreInt, (obj), TYPE_ESP32_CROSSCORE_INT)

typedef struct Esp32CrosscoreInt {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irqs[ESP32_DPORT_CROSSCORE_INT_COUNT];
} Esp32CrosscoreInt;


#define TYPE_ESP32_DPORT "misc.esp32.dport"
#define ESP32_DPORT(obj) OBJECT_CHECK(Esp32DportState, (obj), TYPE_ESP32_DPORT)

typedef struct Esp32DportState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int cpu_count;
    Esp32IntMatrixState intmatrix;
    Esp32CrosscoreInt   crosscore_int;
} Esp32DportState;


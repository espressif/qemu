#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"
#include "hw/misc/esp32_intmatrix.h"

#define TYPE_ESP32_DPORT "misc.esp32.dport"
#define ESP32_DPORT(obj) OBJECT_CHECK(Esp32DportState, (obj), TYPE_ESP32_DPORT)


typedef struct ESP32CrossCoreIntState {
    hwaddr base_addr;
    qemu_irq* irqs;
} ESP32CrossCoreIntState;

typedef struct Esp32DportState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int cpu_count;
    Esp32IntMatrixState intmatrix[ESP32_CPU_COUNT];
} Esp32DportState;


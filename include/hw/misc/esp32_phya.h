#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"


#define TYPE_ESP32_PHYA "misc.esp32.phya"
#define ESP32_PHYA(obj) OBJECT_CHECK(Esp32PhyaState, (obj), TYPE_ESP32_PHYA)

typedef struct Esp32PhyaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t mem[1024];
} Esp32PhyaState;



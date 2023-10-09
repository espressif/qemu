#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"


#define TYPE_ESP32_FE "misc.esp32.fe"
#define ESP32_FE(obj) OBJECT_CHECK(Esp32FeState, (obj), TYPE_ESP32_FE)

typedef struct Esp32FeState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t mem[1024];
} Esp32FeState;



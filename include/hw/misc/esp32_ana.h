#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32_reg.h"


#define TYPE_ESP32_ANA "misc.esp32.ana"
#define ESP32_ANA(obj) OBJECT_CHECK(Esp32AnaState, (obj), TYPE_ESP32_ANA)

typedef struct Esp32AnaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t mem[1024];
} Esp32AnaState;


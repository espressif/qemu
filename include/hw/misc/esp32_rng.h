#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"


#define TYPE_ESP32_RNG "misc.esp32.rng"
#define ESP32_RNG(obj) OBJECT_CHECK(Esp32RngState, (obj), TYPE_ESP32_RNG)

typedef struct Esp32RngState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
} Esp32RngState;

#define ESP32_RNG_BASE (DR_REG_WDEV_BASE + 0x144)


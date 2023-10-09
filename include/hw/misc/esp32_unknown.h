#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32_reg.h"

#define TYPE_ESP32_UNKNOWN "misc.esp32.unknown"
#define ESP32_UNKNOWN(obj) OBJECT_CHECK(Esp32UnknownState, (obj), TYPE_ESP32_UNKNOWN)

typedef struct Esp32UnknownState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
} Esp32UnknownState;


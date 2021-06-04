#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

typedef struct SsiPsramState {
    SSIPeripheral parent_obj;
    uint32_t size_mbytes;
    int dummy;
    int command;
    int byte_count;
} SsiPsramState;

#define TYPE_SSI_PSRAM "ssi_psram"
OBJECT_DECLARE_SIMPLE_TYPE(SsiPsramState, SSI_PSRAM)


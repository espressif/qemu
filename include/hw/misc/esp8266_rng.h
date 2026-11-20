/*
 * ESP8266 hardware random number generator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_ESP8266_RNG_H
#define HW_MISC_ESP8266_RNG_H

#include "hw/sysbus.h"

#define TYPE_ESP8266_RNG "misc.esp8266.rng"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266RngState, ESP8266_RNG)

struct Esp8266RngState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
};

#endif

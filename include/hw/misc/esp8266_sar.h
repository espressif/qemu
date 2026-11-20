/*
 * ESP8266 SAR ADC (TOUT/A0), see hw/misc/esp8266_sar.c
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_ESP8266_SAR_H
#define HW_MISC_ESP8266_SAR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ESP8266_SAR "esp8266.sar"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266SarState, ESP8266_SAR)

#define ESP8266_SAR_MAX 1024

struct Esp8266SarState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint16_t adc;               /* analogRead(A0) value, 0..ESP8266_SAR_MAX */
};

#endif

/*
 * ESP32-H2 INTPRI -- software-triggered cross-core interrupts
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c6_intpri.h"

/*
 * The ESP32-H2 INTPRI block is identical to the ESP32-C6's: same register
 * offsets (CPU_INTR_FROM_CPU_x at 0x90..0x9C), same 4 software interrupt
 * lines, same IO region size.
 */

#define TYPE_ESP32H2_INTPRI "esp32h2.intpri"
#define ESP32H2_INTPRI(obj) OBJECT_CHECK(ESP32C6IntpriState, (obj), TYPE_ESP32H2_INTPRI)

typedef ESP32C6IntpriState ESP32H2IntpriState;

#define ESP32H2_INTPRI_CPU_INTR_COUNT ESP32C6_INTPRI_CPU_INTR_COUNT

/*
 * ESP32-H4 INTPRI — software-triggered cross-core interrupts
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_intpri.h"

#define TYPE_ESP32H4_INTPRI "esp32h4.intpri"
#define ESP32H4_INTPRI(obj) OBJECT_CHECK(ESP32H4IntpriState, (obj), TYPE_ESP32H4_INTPRI)

#define ESP32H4_INTPRI_CPU_INTR_COUNT ESP32C5_INTPRI_CPU_INTR_COUNT

typedef struct ESP32H4IntpriState {
    ESP32C5IntpriState parent;
} ESP32H4IntpriState;

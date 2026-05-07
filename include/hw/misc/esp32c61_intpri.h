/*
 * ESP32-C61 INTPRI — software-triggered cross-core interrupts
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c5_intpri.h"

#define TYPE_ESP32C61_INTPRI "esp32c61.intpri"
#define ESP32C61_INTPRI(obj) OBJECT_CHECK(ESP32C61IntpriState, (obj), TYPE_ESP32C61_INTPRI)

#define ESP32C61_INTPRI_CPU_INTR_COUNT ESP32C5_INTPRI_CPU_INTR_COUNT

typedef struct ESP32C61IntpriState {
    ESP32C5IntpriState parent;
} ESP32C61IntpriState;

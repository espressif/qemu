/*
 * ESP32-C6 AES accelerator, also used by ESP32-C5, ESP32-H2 and ESP32-H4
 *
 * These chips have the registers of the shared esp_aes model at the same
 * offsets. Their extra registers, for pseudo rounds and for GCM on ESP32-H2,
 * are not used by IDF's default config and are not modelled. ESP32-P4 has its
 * own type, which adds the GCM block mode.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp_aes.h"

#define TYPE_ESP32C6_AES "misc.esp32c6.aes"
#define TYPE_ESP32P4_AES "misc.esp32p4.aes"

typedef struct ESP32C6AesState {
    ESPAesState parent;
} ESP32C6AesState;

typedef struct ESP32C6AesClass {
    ESPAesClass parent_class;
} ESP32C6AesClass;

/*
 * ESP32-P4 SHA accelerator
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp32c3_sha.h"

#define TYPE_ESP32P4_SHA "misc.esp32p4.sha"
#define ESP32P4_SHA(obj)           OBJECT_CHECK(ESP32P4ShaState, (obj), TYPE_ESP32P4_SHA)
#define ESP32P4_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32P4ShaClass, obj, TYPE_ESP32P4_SHA)
#define ESP32P4_SHA_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32P4ShaClass, klass, TYPE_ESP32P4_SHA)

/* SHA_M_MEM is 128 bytes wide: SOC_SHA_SUPPORT_SHA512_T is set on the P4 */
#define ESP32P4_SHA_MESSAGE_SIZE    128
#define ESP32P4_SHA_MESSAGE_WORDS   (ESP32P4_SHA_MESSAGE_SIZE / sizeof(uint32_t))

/* SHA_DATE reset value, 538972713 decimal in the IDF header */
#define ESP32P4_SHA_DATE_REG_VALUE  0x20201229

typedef struct ESP32P4ShaState {
    ESP32C3ShaState parent;
} ESP32P4ShaState;

typedef struct ESP32P4ShaClass {
    ESP32C3ShaClass parent_class;
} ESP32P4ShaClass;

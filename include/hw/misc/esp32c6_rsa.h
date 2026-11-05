/*
 * RSA accelerator of ESP32-C6, also used by ESP32-C5 and ESP32-H2, and of ESP32-P4
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp_rsa.h"

#define TYPE_ESP32C6_RSA "misc.esp32c6.rsa"
#define TYPE_ESP32P4_RSA "misc.esp32p4.rsa"

/* Value of RSA_DATE_REG on all of these chips, from their rsa_reg.h in IDF */
#define ESP32C6_RSA_DATE_REG_VALUE  0x20200618

/* Operand sizes follow SOC_RSA_MAX_BIT_LEN: 3072 bits, and 4096 bits on ESP32-P4 */
#define ESP32C6_RSA_MEM_BLK_SIZE    384
#define ESP32P4_RSA_MEM_BLK_SIZE    512

typedef struct ESP32C6RsaState {
    ESPRsaState parent;
} ESP32C6RsaState;

typedef struct ESP32C6RsaClass {
    ESPRsaClass parent_class;
} ESP32C6RsaClass;

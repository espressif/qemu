#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32_reg.h"

#define TYPE_ESP32_AES "misc.esp32.aes"
#define ESP32_AES(obj) OBJECT_CHECK(Esp32AesState, (obj), TYPE_ESP32_AES)

#define ESP32_AES_TEXT_REG_CNT 4
#define ESP32_AES_KEY_REG_CNT 8

#define ESP32_AES_ENCRYPTION_MODE TRUE
#define ESP32_AES_DECRYPTION_MODE FALSE

typedef struct Esp32AesState {
    SysBusDevice parent_object;
    MemoryRegion iomem;
    uint32_t text[ESP32_AES_TEXT_REG_CNT];
    uint32_t key[ESP32_AES_KEY_REG_CNT];
    uint32_t aes_idle_reg;
    struct {
        bool type;
        int bits;
    } mode;
} Esp32AesState;

REG32(AES_START_REG, 0x00)
REG32(AES_IDLE_REG, 0x04)
REG32(AES_MODE_REG, 0x08)
REG32(AES_ENDIAN_REG, 0x40)

REG32(AES_KEY_0_REG, 0x10)
REG32(AES_KEY_1_REG, 0x14)
REG32(AES_KEY_2_REG, 0x18)
REG32(AES_KEY_3_REG, 0x1C)
REG32(AES_KEY_4_REG, 0x20)
REG32(AES_KEY_5_REG, 0x24)
REG32(AES_KEY_6_REG, 0x28)
REG32(AES_KEY_7_REG, 0x2C)

REG32(AES_TEXT_0_REG, 0x30)
REG32(AES_TEXT_1_REG, 0x34)
REG32(AES_TEXT_2_REG, 0x38)
REG32(AES_TEXT_3_REG, 0x3C)

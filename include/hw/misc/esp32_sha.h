#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32_reg.h"

#define TYPE_ESP32_SHA "misc.esp32.sha"
#define ESP32_SHA(obj) OBJECT_CHECK(Esp32ShaState, (obj), TYPE_ESP32_SHA)

#define ESP32_SHA_TEXT_REG_CNT    32

typedef struct Esp32ShaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t text[ESP32_SHA_TEXT_REG_CNT];
    uint8_t* full_text;
    uint32_t full_text_reserved;
    uint32_t full_text_len;
} Esp32ShaState;

#define SHA_REG_GROUP(name, base) \
    REG32(name ## _START, (base)) \
    REG32(name ## _CONTINUE, (base + 0x4)) \
    REG32(name ## _LOAD, (base + 0x8)) \
    REG32(name ## _BUSY, (base + 0xc))

SHA_REG_GROUP(SHA1, 0x80)
SHA_REG_GROUP(SHA256, 0x90)
SHA_REG_GROUP(SHA384, 0xa0)
SHA_REG_GROUP(SHA512, 0xb0)

#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "target/xtensa/cpu.h"
#include "target/xtensa/cpu-qom.h"
#include "hw/misc/esp32_reg.h"
#define TYPE_ESP32_RTC_CNTL "misc.esp32.rtc_cntl"
#define ESP32_RTC_CNTL(obj) OBJECT_CHECK(Esp32RtcCntlState, (obj), TYPE_ESP32_RTC_CNTL)

typedef struct Esp32RtcCntlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t scratch_reg[ESP32_RTC_CNTL_SCRATCH_REG_COUNT];
} Esp32RtcCntlState;

REG32(RTC_CNTL_STORE0, 0x4c)
REG32(RTC_CNTL_STORE1, 0x50)
REG32(RTC_CNTL_STORE2, 0x54)
REG32(RTC_CNTL_STORE3, 0x58)
REG32(RTC_CNTL_STORE4, 0xb0)
REG32(RTC_CNTL_STORE5, 0xb4)
REG32(RTC_CNTL_STORE6, 0xb8)
REG32(RTC_CNTL_STORE7, 0xbc)
REG32(RTC_CNTL_DATE,   0x13c)

#define ESP32_RTC_CNTL_SIZE (A_RTC_CNTL_DATE + 4)

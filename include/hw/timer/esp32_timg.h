#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"

#define TYPE_ESP32_TIMG "timer.esp32.timg"
#define ESP32_TIMG(obj) OBJECT_CHECK(Esp32TimgState, (obj), TYPE_ESP32_TIMG)

typedef enum Esp32TimgCalClkSel {
    ESP32_TIMG_CAL_RTC_MUX = 0,
    ESP32_TIMG_CAL_8MD256 = 1,
    ESP32_TIMG_CAL_32K_XTAL = 2
} Esp32TimgCalClkSel;

typedef struct Esp32TimgState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t rtc_slow_freq_hz;
    uint32_t xtal_freq_hz;

    bool rtc_cal_start;
    bool rtc_cal_ready;
    Esp32TimgCalClkSel rtc_cal_clk_sel;
    uint32_t rtc_cal_max;
    uint32_t rtc_cal_value;
} Esp32TimgState;

#define RTC_CLK_CAL_FRACT  19
#define RTC_CLK_CAL_FACTOR  (1 << RTC_CLK_CAL_FRACT)

REG32(TIMG_RTCCALICFG, 0x68)
    FIELD(TIMG_RTCCALICFG, START, 31, 1)
    FIELD(TIMG_RTCCALICFG, MAX, 16, 15)
    FIELD(TIMG_RTCCALICFG, RDY, 15, 1)
    FIELD(TIMG_RTCCALICFG, CLK_SEL, 13, 2)
    FIELD(TIMG_RTCCALICFG, START_CYCLING, 12, 1)

REG32(TIMG_RTCCALICFG1, 0x6c)
    FIELD(TIMG_RTCCALICFG1, VALUE, 7, 25)

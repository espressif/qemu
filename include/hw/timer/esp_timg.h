/*
 * Timer Group emulation for recent ESP32-series chip (ESP32-S3 and newer)
 *
 * Copyright (c) 2023-2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"


#define TYPE_ESP_TIMG               "timer.esp.timg"
#define ESP_TIMG(obj)               OBJECT_CHECK(ESPTimgState, (obj), TYPE_ESP_TIMG)
#define ESP_TIMG_GET_CLASS(obj)     OBJECT_GET_CLASS(ESPTimgClass, obj, TYPE_ESP_TIMG)
#define ESP_TIMG_CLASS(klass)       OBJECT_CLASS_CHECK(ESPTimgClass, klass, TYPE_ESP_TIMG)


/**
 * Size of the Timegroup I/O registers area
 */
#define ESP_TIMG_IO_SIZE (A_TIMGCLK + 4)

/**
 * Values related to the TIMG T0 counter
 */
#define ESP_TIMG_T0_MAX_VALUE   ((1ULL << 54) - 1)
/* Limit value is used to calculate the distance between the alarm and the counter */
#define ESP_TIMG_T0_LIMIT       (1ULL << 53)

/**
 * Value of each calibration clock available
 */
#define ESP_TIMG_CALI_RC_SLOW_CLK       0
#define ESP_TIMG_CALI_RC_FAST_DIV_CLK   1
#define ESP_TIMG_CALI_XTAL32K_CLK       2

/**
 * And their associated frequencies
 */
#define ESP_APB_CLK             80000000UL
#define ESP_XTAL_CLK            40000000UL
#define ESP_RC_SLOW_FREQ        136000
#define ESP_RC_FAST_FREQ        17500000
#define ESP_RC_FAST_DIV_FREQ    (ESP_RC_FAST_FREQ / 256)
#define ESP_XTAL32K_FREQ        32768


/**
 * Number of stages in the a single Watchdog timer
 */
#define ESP_WDT_STAGE_COUNT     4

/**
 * Default key value for the WKEY register.
 */
#define ESP_WDT_DEFAULT_WKEY     0x50d83aa1

/**
 * Define two names for the WDT's interrupt IRQ and reset IRQ respectively
 */
#define ESP_WDT_IRQ_RESET           "wdt-reset"
#define ESP_WDT_IRQ_INTERRUPT       "wdt-interrupt"


#define ESP_T0_IRQ_INTERRUPT    "t0-interrupt"
#define ESP_T1_IRQ_INTERRUPT    "t1-interrupt"


typedef enum {
    ESP_WDT_OFF       = 0,
    ESP_WDT_INTERRUPT = 1,
    ESP_WDT_RESET_CPU = 2,
    ESP_WDT_RESET_SYS = 3,
} ESPWdtStageConf;


typedef struct ESPVirtualCounter {
    QEMUTimer timer;
    /* Timer current value in ticks */
    uint64_t value;
    /* Time when the value was last updated */
    uint64_t base;
    /* Frequency, in Hz, of the timer */
    uint64_t frequency;
} ESPVirtualCounter;


typedef struct ESPWdtState {
    /* Store the configuration register as is, it will ease reads perform to it */
    uint32_t config0;
    /* Only keep the prescaler field for the config1 register */
    uint32_t prescaler;
    /* Value of each stage, in MWDT clock cycles! (CLK / Prescaler) */
    uint32_t stage[ESP_WDT_STAGE_COUNT];
    /* Value used to protect writes to the registers */
    uint32_t wkey;
    /* Raw status of the interrupt */
    int raw_st;
    bool int_enabled;

    /* These are mirror values that are written by the software */
    uint32_t prescaler_mirror;
    uint32_t stage_mirror[ESP_WDT_STAGE_COUNT];

    /* "Private" members, not accessible by the software */
    /* Mirror of the stages configuration */
    ESPWdtStageConf stage_conf[ESP_WDT_STAGE_COUNT];
    /* The stage is comprised between 0 and ESP_WDT_STAGE_COUNT */
    int current_stage;
    ESPVirtualCounter counter;
    qemu_irq reset_irq;
    qemu_irq interrupt_irq;
} ESPWdtState;


typedef struct ESPT0State {
    uint32_t config;
    /* Register containing the alarm value */
    uint64_t alarm;
    /* Relative value that will be inc/dec according to the configuration */
    uint64_t value_rel;
    /* Register containing the current counter value after a flush request */
    uint64_t value_flushed;
    /* Register containing the value to copy to the counter */
    uint64_t value_toload;

    /* Raw status of the interrupt */
    int raw_st;
    bool int_enabled;
    ESPVirtualCounter counter;
    qemu_irq interrupt_irq;
} ESPT0State;


typedef struct ESPRtcState {
    uint32_t rtc_cali_cfg;
    /* Register storing the result of calibration (RTCCALICFG1) */
    uint32_t rtc_cali_cfg_result;
    /* Register storing the calibration timeout (RTCCALICFG2) */
    uint32_t rtc_cali_cfg_timeout;
} ESPRtcState;


typedef struct ESPTimgState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ESPT0State t0;
    ESPT0State t1;
    ESPWdtState wdt;
    ESPRtcState rtc;

    /* Property used to disable the watchdog from command line */
    bool wdt_disable;

    /* Per-machine clock frequencies (Hz) */
    uint32_t xtal_freq;
    /*
     * Set for SoCs where WDTCONFIG0 has no USE_XTAL bit because the MWDT is
     * always clocked from XTAL, the source being selected in the clock/reset
     * controller instead (ESP32-P4).
     */
    bool wdt_clk_is_xtal;
    /*
     * Calibration CLK_SEL → oscillator frequency mapping.
     * Index = register CLK_SEL value, value = frequency in Hz.
     * Non-zero entries override the default (C3-compatible) mapping.
     */
    uint32_t cali_osc_freq[8];
} ESPTimgState;


typedef struct ESPTimgClass {
    SysBusDeviceClass parent_class;
    /* Virtual attribute */
    size_t m_has_t1;
} ESPTimgClass;


REG32(TIMG_T0CONFIG, 0x0000)
    FIELD(TIMG_T0CONFIG, EN, 31, 1)
    FIELD(TIMG_T0CONFIG, INCREASE, 30, 1)
    FIELD(TIMG_T0CONFIG, AUTORELOAD, 29, 1)
    FIELD(TIMG_T0CONFIG, DIVIDER, 13, 16)
    FIELD(TIMG_T0CONFIG, DIVCNT_RST, 12, 1)
    FIELD(TIMG_T0CONFIG, ALARM_EN, 10, 1)
    FIELD(TIMG_T0CONFIG, USE_XTAL, 9, 1)

REG32(TIMG_T0LO, 0x0004)
    FIELD(TIMG_T0LO, LO, 0, 32)

REG32(TIMG_T0HI, 0x0008)
    FIELD(TIMG_T0HI, HI, 0, 22)

REG32(TIMG_T0UPDATE, 0x000c)
    FIELD(TIMG_T0UPDATE, UPDATE, 31, 1)

REG32(TIMG_T0ALARMLO, 0x0010)
    FIELD(TIMG_T0ALARMLO, ALARM_LO, 0, 32)

REG32(TIMG_T0ALARMHI, 0x0014)
    FIELD(TIMG_T0ALARMHI, ALARM_HI, 0, 22)

REG32(TIMG_T0LOADLO, 0x0018)
    FIELD(TIMG_T0LOADLO, LOAD_LO, 0, 32)

REG32(TIMG_T0LOADHI, 0x001c)
    FIELD(TIMG_T0LOADHI, LOAD_HI, 0, 22)

REG32(TIMG_T0LOAD, 0x0020)
    FIELD(TIMG_T0LOAD, LOAD, 0, 32)

/* Define necessary T1 registers even though they may not be used */
REG32(TIMG_T1CONFIG, 0x0024)
REG32(TIMG_T1LOAD,   0x0044)


REG32(TIMG_WDTCONFIG0, 0x0048)
    FIELD(TIMG_WDTCONFIG0, EN, 31, 1)
    FIELD(TIMG_WDTCONFIG0, STG0, 29, 2)
    FIELD(TIMG_WDTCONFIG0, STG1, 27, 2)
    FIELD(TIMG_WDTCONFIG0, STG2, 25, 2)
    FIELD(TIMG_WDTCONFIG0, STG3, 23, 2)
    FIELD(TIMG_WDTCONFIG0, CONF_UPDATE_EN, 22, 1)
    FIELD(TIMG_WDTCONFIG0, USE_XTAL, 21, 1)
    FIELD(TIMG_WDTCONFIG0, CPU_RESET_LENGTH, 18, 3)
    FIELD(TIMG_WDTCONFIG0, SYS_RESET_LENGTH, 15, 3)
    FIELD(TIMG_WDTCONFIG0, FLASHBOOT_MOD_EN, 14, 1)
    FIELD(TIMG_WDTCONFIG0, PROCPU_RESET_EN, 13, 1)
    FIELD(TIMG_WDTCONFIG0, APPCPU_RESET_EN, 12, 1)

REG32(TIMG_WDTCONFIG1, 0x004c)
    FIELD(TIMG_WDTCONFIG1, CLK_PRESCALE, 16, 16)
    FIELD(TIMG_WDTCONFIG1, DIVCNT_RST, 0, 1)

REG32(TIMG_WDTCONFIG2, 0x0050)
    FIELD(TIMG_WDTCONFIG2, STG0_HOLD, 0, 32)

REG32(TIMG_WDTCONFIG3, 0x0054)
    FIELD(TIMG_WDTCONFIG3, STG1_HOLD, 0, 32)

REG32(TIMG_WDTCONFIG4, 0x0058)
    FIELD(TIMG_WDTCONFIG4, STG2_HOLD, 0, 32)

REG32(TIMG_WDTCONFIG5, 0x005c)
    FIELD(TIMG_WDTCONFIG5, STG3_HOLD, 0, 32)

REG32(TIMG_WDTFEED, 0x0060)
    FIELD(TIMG_WDTFEED, FEED, 0, 32)

REG32(TIMG_WDTWPROTECT, 0x0064)
    FIELD(TIMG_WDTWPROTECT, WKEY, 0, 32)

REG32(TIMG_RTCCALICFG, 0x0068)
    FIELD(TIMG_RTCCALICFG, START, 31, 1)
    FIELD(TIMG_RTCCALICFG, MAX, 16, 15)
    FIELD(TIMG_RTCCALICFG, RDY, 15, 1)
    FIELD(TIMG_RTCCALICFG, CLK_SEL, 13, 2)
    FIELD(TIMG_RTCCALICFG, START_CYCLING, 12, 1)

REG32(TIMG_RTCCALICFG1, 0x006c)
    FIELD(TIMG_RTCCALICFG1, VALUE, 7, 25)
    FIELD(TIMG_RTCCALICFG1, CYCLING_DATA_VLD, 0, 1)


REG32(TIMG_INT_ENA_TIMG, 0x0070)
REG32(TIMG_INT_RAW_TIMG, 0x0074)
REG32(TIMG_INT_ST_TIMG,  0x0078)
REG32(TIMG_INT_CLR_TIMG, 0x007C)


REG32(TIMG_T0_INT_TIMG, 0x0074)
REG32(TIMG_T0T1_INT_TIMG, 0x0074)
    /* T1-able targets */
    FIELD(TIMG_T0T1_INT_TIMG, WDT_RAW, 2, 1)
    FIELD(TIMG_T0T1_INT_TIMG, T1_RAW,  1, 1)
    FIELD(TIMG_T0T1_INT_TIMG, T0_RAW,  0, 1)
    /* T0-only targets */
    FIELD(TIMG_T0_INT_TIMG, WDT_RAW, 1, 1)
    FIELD(TIMG_T0_INT_TIMG, T0_RAW,  0, 1)
#define R_TIMG_T0_INT_TIMG_T1_RAW_MASK      0
#define R_TIMG_T0_INT_TIMG_T1_RAW_SHIFT     31 // It will result in a 0 value on 32-bit CPUs

REG32(TIMG_RTCCALICFG2, 0x0080)
    FIELD(TIMG_RTCCALICFG2, TIMEOUT_THRES, 7, 25)
    FIELD(TIMG_RTCCALICFG2, TIMEOUT_RST_CNT, 3, 4)
    FIELD(TIMG_RTCCALICFG2, TIMEOUT, 0, 1)

REG32(TIMG_NTIMG_DATE, 0x00f8)
    FIELD(TIMG_NTIMG_DATE, TIMG_NTIMGS_DATE, 0, 28)

REG32(TIMGCLK, 0x00fc)
    FIELD(TIMGCLK, CLK_EN, 31, 1)
    FIELD(TIMGCLK, TIMER_CLK_IS_ACTIVE, 30, 1)
    FIELD(TIMGCLK, WDT_CLK_IS_ACTIVE, 29, 1)

#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"


#define TYPE_ESP32C3_TIMG "timer.esp32c3.timg"
#define ESP32C3_TIMG(obj)           OBJECT_CHECK(ESP32C3TimgState, (obj), TYPE_ESP32C3_TIMG)
#define ESP32C3_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3TimgState, obj, TYPE_ESP32C3_TIMG)
#define ESP32C3_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C3TimgState, klass, TYPE_ESP32C3_TIMG)


/**
 * Size of the Timegroup I/O registers area
 */
#define ESP32C3_TIMG_IO_SIZE (A_TIMGCLK + 4)

/**
 * Values related to the TIMG T0 counter
 */
#define ESP32C3_TIMG_T0_MAX_VALUE   ((1ULL << 54) - 1)
/* Limit value is used to calculate the distance between the alarm and the counter */
#define ESP32C3_TIMG_T0_LIMIT       (1ULL << 53)

/**
 * Value of each calibration clock available
 */
#define ESP32C3_TIMG_CALI_RC_SLOW_CLK       0
#define ESP32C3_TIMG_CALI_RC_FAST_DIV_CLK   1
#define ESP32C3_TIMG_CALI_XTAL32K_CLK       2

/**
 * And their associated frequencies
 */
#define ESP32C3_APB_CLK             80000000UL
#define ESP32C3_XTAL_CLK            40000000UL
#define ESP32C3_RC_SLOW_FREQ        136000
#define ESP32C3_RC_FAST_FREQ        17500000
#define ESP32C3_RC_FAST_DIV_FREQ    (ESP32C3_RC_FAST_FREQ / 256)
#define ESP32C3_XTAL32K_FREQ        32000


/**
 * Number of stages in the a single Watchdog timer
 */
#define ESP32C3_WDT_STAGE_COUNT     4

/**
 * Default key value for the WKEY register.
 */
#define ESP32C3_WDT_DEFAULT_WKEY     0x50d83aa1

/**
 * Define two names for the WDT's interrupt IRQ and reset IRQ respectively
 */
#define ESP32C3_WDT_IRQ_RESET           "wdt-reset"
#define ESP32C3_WDT_IRQ_INTERRUPT       "wdt-interrupt"


#define ESP32C3_T0_IRQ_INTERRUPT    "t0-interrupt"


typedef enum {
    ESP32C3_WDT_OFF       = 0,
    ESP32C3_WDT_INTERRUPT = 1,
    ESP32C3_WDT_RESET_CPU = 2,
    ESP32C3_WDT_RESET_SYS = 3,
} ESP32C3WdtStageConf;


typedef struct ESP32C3VirtualCounter {
    QEMUTimer timer;
    /* Timer current value in ticks */
    uint64_t value;
    /* Time when the value was last updated */
    uint64_t base;
    /* Frequency, in Hz, of the timer */
    uint64_t frequency;
} ESP32C3VirtualCounter;


typedef struct ESP32C3WdtState {
    /* Store the configuration register as is, it will ease reads perform to it */
    uint32_t config0;
    /* Only keep the prescaler field for the config1 register */
    uint32_t prescaler;
    /* Value of each stage, in MWDT clock cycles! (CLK / Prescaler) */
    uint32_t stage[ESP32C3_WDT_STAGE_COUNT];
    /* Value used to protect writes to the registers */
    uint32_t wkey;
    /* Raw status of the interrupt */
    int raw_st;
    bool int_enabled;

    /* These are mirror values that are written by the software */
    uint32_t prescaler_mirror;
    uint32_t stage_mirror[ESP32C3_WDT_STAGE_COUNT];

    /* "Private" members, not accessible by the software */
    /* Mirror of the stages configuration */
    ESP32C3WdtStageConf stage_conf[ESP32C3_WDT_STAGE_COUNT];
    /* The stage is comprised between 0 and ESP32C3_WDT_STAGE_COUNT */
    int current_stage;
    ESP32C3VirtualCounter counter;
    qemu_irq reset_irq;
    qemu_irq interrupt_irq;
} ESP32C3WdtState;


typedef struct ESP32C3T0State {
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
    ESP32C3VirtualCounter counter;
    qemu_irq interrupt_irq;
} ESP32C3T0State;


typedef struct ESP32C3RtcState {
    uint32_t rtc_cali_cfg;
    /* Register storing the result of calibration (RTCCALICFG1) */
    uint32_t rtc_cali_cfg_result;
    /* Register storing the calibration timeout (RTCCALICFG2) */
    uint32_t rtc_cali_cfg_timeout;
} ESP32C3RtcState;


typedef struct ESP32C3TimgState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ESP32C3T0State t0;
    ESP32C3WdtState wdt;
    ESP32C3RtcState rtc;

    /* Property used to disable the watchdog from command line */
    bool wdt_disable;
} ESP32C3TimgState;


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
    FIELD(TIMG_INT_ENA_TIMG, WDT_ENA, 1, 1)
    FIELD(TIMG_INT_ENA_TIMG, T0_ENA, 0, 1)

REG32(TIMG_INT_RAW_TIMG, 0x0074)
    FIELD(TIMG_INT_RAW_TIMG, WDT_RAW, 1, 1)
    FIELD(TIMG_INT_RAW_TIMG, T0_RAW, 0, 1)

REG32(TIMG_INT_ST_TIMG, 0x0078)
    FIELD(TIMG_INT_ST_TIMG, WDT_ST, 1, 1)
    FIELD(TIMG_INT_ST_TIMG, T0_ST, 0, 1)

REG32(TIMG_INT_CLR_TIMG, 0x007c)
    FIELD(TIMG_INT_CLR_TIMG, WDT_CLR, 1, 1)
    FIELD(TIMG_INT_CLR_TIMG, T0_CLR, 0, 1)

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

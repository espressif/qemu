/*
 * ESP32-C3 System Timer emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"


#define TYPE_ESP32C3_SYSTIMER "esp32c3.systimer"
#define ESP32C3_SYSTIMER(obj)           OBJECT_CHECK(ESP32C3SysTimerState, (obj), TYPE_ESP32C3_SYSTIMER)
#define ESP32C3_SYSTIMER_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3SysTimerState, obj, TYPE_ESP32C3_SYSTIMER)
#define ESP32C3_SYSTIMER_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C3SysTimerState, klass, TYPE_ESP32C3_SYSTIMER)


/**
 * Size of the System Timer I/O registers area
 */
#define ESP32C3_SYSTIMER_IO_SIZE (A_SYSTIMER_DATE + 4)

/**
 * Mask to get a 52 value out of a 64 bit value
 */
#define ESP32C3_SYSTIMER_52BIT_MASK ((1ULL << 52) - 1)

/**
 * Frequency of the CNT_CLK
 */
#define ESP32C3_SYSTIMER_CNT_CLK    16000000

/**
 * Ticks per microsecond
 */
#define ESP32C3_SYSTIMER_CNT_PER_US    (ESP32C3_SYSTIMER_CNT_CLK / 1000000)

/**
 * Macro to help updating part of a register/variable
 */
static inline void systimer_set_reg(uint64_t* reg, uint64_t value, uint64_t mask, uint64_t shift)
{
    const uint64_t new_val = (value & mask) << shift;
    *reg = (*reg & ~(mask << shift)) | new_val;
}

#define SYSTIMER_GET_REG(reg, shift, mask) (((reg) >> (shift)) & (mask))

/**
 * Number of comparator in a single System Timer module
 */
#define ESP32C3_SYSTIMER_COMP_COUNT     3

/**
 * Number of counters in a single System Time module
 */
#define ESP32C3_SYSTIMER_COUNTER_COUNT  2

/**
 * Number of interrupt line of the System Time module
 */
#define ESP32C3_SYSTIMER_IRQ_COUNT      (ESP32C3_SYSTIMER_COMP_COUNT)


typedef struct {
    bool enabled;
    /* Enabled on CPU stall */
    bool enabled_on_stall;
    uint64_t value;   // Internal counter that should be updated as often as possible
    uint64_t toload;  // Counter that can be loaded by the guest program
    uint64_t flushed; // Mirror of the internal counter that can be seen by the guest program
    /* Absolute time, in ns, when value was updated last */
    int64_t base;
} ESP32C3SysTimerCounter;


/* This typedef must be placed here as it will be used by the structure below */
typedef struct ESP32C3SysTimerState ESP32C3SysTimerState;

typedef struct {
    uint64_t value;
    uint64_t value_toload;
    uint32_t period;
    uint32_t period_toload;
    /* Mark whether the comparator is enabled */
    bool enabled;
    bool period_mode;
    /* Raw status of the comparator: 1 if it reached the threshold, 0 else */
    bool raw_st;
    bool int_enabled;
    /* Index of the counter that is linked to the comparator */
    uint32_t counter;
    /* The easiest way to implement timeout is by using one timer per comparator */
    QEMUTimer qtimer;
    uint64_t expire_time;
    qemu_irq irq;
    int cur_irq_level;
    /* Pointer to the owner */
    ESP32C3SysTimerState* systimer;
} ESP32C3SysTimerComp;


struct ESP32C3SysTimerState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    /* Mirror of the comparators and counters state, only used to speed up
     * reading of A_SYSTIMER_CONF register  */
    uint32_t conf;
    ESP32C3SysTimerCounter counter[ESP32C3_SYSTIMER_COUNTER_COUNT];
    ESP32C3SysTimerComp comparators[ESP32C3_SYSTIMER_COMP_COUNT];
};


REG32(SYSTIMER_CONF, 0x0000)
    FIELD(SYSTIMER_CONF, CLK_EN, 31, 1)
    FIELD(SYSTIMER_CONF, TIMER_UNIT0_WORK_EN, 30, 1)
    FIELD(SYSTIMER_CONF, TIMER_UNIT1_WORK_EN, 29, 1)
    FIELD(SYSTIMER_CONF, TIMER_UNIT0_CORE0_STALL_EN, 28, 1)
    FIELD(SYSTIMER_CONF, TIMER_UNIT0_CORE1_STALL_EN, 27, 1)
    FIELD(SYSTIMER_CONF, TIMER_UNIT1_CORE0_STALL_EN, 26, 1)
    FIELD(SYSTIMER_CONF, TIMER_UNIT1_CORE1_STALL_EN, 25, 1)
    FIELD(SYSTIMER_CONF, TARGET0_WORK_EN, 24, 1)
    FIELD(SYSTIMER_CONF, TARGET1_WORK_EN, 23, 1)
    FIELD(SYSTIMER_CONF, TARGET2_WORK_EN, 22, 1)

REG32(SYSTIMER_UNIT0_OP, 0x0004)
    FIELD(SYSTIMER_UNIT0_OP, UPDATE, 30, 1)
    FIELD(SYSTIMER_UNIT0_OP, VALUE_VALID, 29, 1)

REG32(SYSTIMER_UNIT1_OP, 0x0008)
    FIELD(SYSTIMER_UNIT1_OP, UPDATE, 30, 1)
    FIELD(SYSTIMER_UNIT1_OP, VALID, 29, 1)

REG32(SYSTIMER_UNIT0_LOAD_HI, 0x000C)
    FIELD(SYSTIMER_UNIT0_LOAD_HI, HI, 0, 20)

REG32(SYSTIMER_UNIT0_LOAD_LO, 0x0010)
    FIELD(SYSTIMER_UNIT0_LOAD_LO, LO, 0, 32)

REG32(SYSTIMER_UNIT1_LOAD_HI, 0x0014)
    FIELD(SYSTIMER_UNIT1_LOAD_HI, HI, 0, 20)

REG32(SYSTIMER_UNIT1_LOAD_LO, 0x0018)
    FIELD(SYSTIMER_UNIT1_LOAD_LO, LO, 0, 32)

REG32(SYSTIMER_TARGET0_HI, 0x001C)
    FIELD(SYSTIMER_TARGET0_HI, HI, 0, 20)

REG32(SYSTIMER_TARGET0_LO, 0x0020)
    FIELD(SYSTIMER_TARGET0_LO, LO, 0, 32)

REG32(SYSTIMER_TARGET1_HI, 0x0024)
    FIELD(SYSTIMER_TARGET1_HI, HI, 0, 20)

REG32(SYSTIMER_TARGET1_LO, 0x0028)
    FIELD(SYSTIMER_TARGET1_LO, LO, 0, 32)

REG32(SYSTIMER_TARGET2_HI, 0x002C)
    FIELD(SYSTIMER_TARGET2_HI, HI, 0, 20)

REG32(SYSTIMER_TARGET2_LO, 0x0030)
    FIELD(SYSTIMER_TARGET2_LO, LO, 0, 32)

REG32(SYSTIMER_TARGET0_CONF, 0x0034)
    FIELD(SYSTIMER_TARGET0_CONF, TIMER_UNIT_SEL, 31, 1)
    FIELD(SYSTIMER_TARGET0_CONF, PERIOD_MODE, 30, 1)
    FIELD(SYSTIMER_TARGET0_CONF, PERIOD, 0, 26)

REG32(SYSTIMER_TARGET1_CONF, 0x0038)
    FIELD(SYSTIMER_TARGET1_CONF, TIMER_UNIT_SEL, 31, 1)
    FIELD(SYSTIMER_TARGET1_CONF, PERIOD_MODE, 30, 1)
    FIELD(SYSTIMER_TARGET1_CONF, PERIOD, 0, 26)

REG32(SYSTIMER_TARGET2_CONF, 0x003C)
    FIELD(SYSTIMER_TARGET2_CONF, TIMER_UNIT_SEL, 31, 1)
    FIELD(SYSTIMER_TARGET2_CONF, PERIOD_MODE, 30, 1)
    FIELD(SYSTIMER_TARGET2_CONF, PERIOD, 0, 26)

REG32(SYSTIMER_UNIT0_VALUE_HI, 0x0040)
    FIELD(SYSTIMER_UNIT0_VALUE_HI, TIMER_HI, 0, 20)

REG32(SYSTIMER_UNIT0_VALUE_LO, 0x0044)
    FIELD(SYSTIMER_UNIT0_VALUE_LO, TIMER_LO, 0, 32)

REG32(SYSTIMER_UNIT1_VALUE_HI, 0x0048)
    FIELD(SYSTIMER_UNIT1_VALUE_HI, TIMER_HI, 0, 20)

REG32(SYSTIMER_UNIT1_VALUE_LO, 0x004C)
    FIELD(SYSTIMER_UNIT1_VALUE_LO, TIMER_LO, 0, 32)

REG32(SYSTIMER_COMP0_LOAD, 0x0050)
    FIELD(SYSTIMER_COMP0_LOAD, LOAD, 0, 1)

REG32(SYSTIMER_COMP1_LOAD, 0x0054)
    FIELD(SYSTIMER_COMP1_LOAD, LOAD, 0, 1)

REG32(SYSTIMER_COMP2_LOAD, 0x0058)
    FIELD(SYSTIMER_COMP2_LOAD, LOAD, 0, 1)

REG32(SYSTIMER_UNIT0_LOAD, 0x005C)
    FIELD(SYSTIMER_UNIT0_LOAD, LOAD, 0, 1)

REG32(SYSTIMER_UNIT1_LOAD, 0x0060)
    FIELD(SYSTIMER_UNIT1_LOAD, LOAD, 0, 1)

REG32(SYSTIMER_INT_ENA, 0x0064)
    FIELD(SYSTIMER_INT_ENA, TARGET2, 2, 1)
    FIELD(SYSTIMER_INT_ENA, TARGET1, 1, 1)
    FIELD(SYSTIMER_INT_ENA, TARGET0, 0, 1)

REG32(SYSTIMER_INT_RAW, 0x0068)
    FIELD(SYSTIMER_INT_RAW, TARGET2, 2, 1)
    FIELD(SYSTIMER_INT_RAW, TARGET1, 1, 1)
    FIELD(SYSTIMER_INT_RAW, TARGET0, 0, 1)

REG32(SYSTIMER_INT_CLR, 0x006c)
    FIELD(SYSTIMER_INT_CLR, TARGET2, 2, 1)
    FIELD(SYSTIMER_INT_CLR, TARGET1, 1, 1)
    FIELD(SYSTIMER_INT_CLR, TARGET0, 0, 1)

REG32(SYSTIMER_INT_ST, 0x0070)
    FIELD(SYSTIMER_INT_ST, TARGET2, 2, 1)
    FIELD(SYSTIMER_INT_ST, TARGET1, 1, 1)
    FIELD(SYSTIMER_INT_ST, TARGET0, 0, 1)

REG32(SYSTIMER_DATE, 0x00fc)
    FIELD(SYSTIMER_DATE, DATE, 0, 28)

#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"

#define TYPE_ESP32_FRC_TIMER "timer.esp32.frc"
#define ESP32_FRC_TIMER(obj) OBJECT_CHECK(Esp32FrcTimerState, (obj), TYPE_ESP32_FRC_TIMER)

typedef struct Esp32FrcTimerState {
    /* private */
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    QEMUTimer alarm_timer;

    /* properties */
    uint32_t apb_freq;
    bool has_alarm;
    uint32_t count_mask;

    /* state */
    uint32_t count_base;
    uint64_t ns_base;
    bool level_int_status;

    /* registers */
    uint32_t load_reg;
    bool enable;
    bool autoload;
    uint32_t prescaler;
    bool level_int;
    uint32_t alarm_reg;
} Esp32FrcTimerState;

REG32(FRC_TIMER_LOAD, 0x00)
REG32(FRC_TIMER_COUNT, 0x04)
REG32(FRC_TIMER_CTRL, 0x08)
    FIELD(FRC_TIMER_CTRL, LEVEL_INT, 0, 1)
    FIELD(FRC_TIMER_CTRL, PRESCALER, 1, 3)
    FIELD(FRC_TIMER_CTRL, AUTOLOAD, 6, 1)
    FIELD(FRC_TIMER_CTRL, ENABLE, 7, 1)
    FIELD(FRC_TIMER_CTRL, STATUS, 8, 1)
REG32(FRC_TIMER_INT_CLR, 0x0c)
REG32(FRC_TIMER_ALARM, 0x10)

#define ESP32_FRC_TIMER_STRIDE  0x20

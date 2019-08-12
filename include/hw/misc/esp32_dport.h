#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"
#include "target/xtensa/cpu.h"
#include "target/xtensa/cpu-qom.h"


#define TYPE_ESP32_INTMATRIX "misc.esp32.intmatrix"
#define ESP32_INTMATRIX(obj) OBJECT_CHECK(Esp32IntMatrixState, (obj), TYPE_ESP32_INTMATRIX)

typedef struct Esp32IntMatrixState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq *outputs[ESP32_CPU_COUNT];
    uint8_t irq_map[ESP32_CPU_COUNT][ESP32_INT_MATRIX_INPUTS];

    /* properties */
    XtensaCPU *cpu[ESP32_CPU_COUNT];
} Esp32IntMatrixState;


#define TYPE_ESP32_CROSSCORE_INT "misc.esp32.crosscoreint"
#define ESP32_CROSSCORE_INT(obj) OBJECT_CHECK(Esp32CrosscoreInt, (obj), TYPE_ESP32_CROSSCORE_INT)

typedef struct Esp32CrosscoreInt {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irqs[ESP32_DPORT_CROSSCORE_INT_COUNT];
} Esp32CrosscoreInt;


#define TYPE_ESP32_DPORT "misc.esp32.dport"
#define ESP32_DPORT(obj) OBJECT_CHECK(Esp32DportState, (obj), TYPE_ESP32_DPORT)

typedef struct Esp32CacheState {
    uint32_t cache_ctrl_reg;
    uint32_t cache_ctrl1_reg;
} Esp32CacheState;

typedef struct Esp32DportState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int cpu_count;
    Esp32IntMatrixState intmatrix;
    Esp32CrosscoreInt   crosscore_int;
    Esp32CacheState cache_state[ESP32_CPU_COUNT];
    qemu_irq appcpu_stall_req;
    qemu_irq appcpu_reset_req;

    bool appcpu_reset_state;
    bool appcpu_stall_state;
    bool appcpu_clkgate_state;
    uint32_t appcpu_boot_addr;
} Esp32DportState;

#define ESP32_DPORT_APPCPU_STALL_GPIO   "appcpu-stall"
#define ESP32_DPORT_APPCPU_RESET_GPIO   "appcpu-reset"


REG32(DPORT_APPCPU_RESET, 0x2c)
REG32(DPORT_APPCPU_CLK, 0x30)
REG32(DPORT_APPCPU_RUNSTALL, 0x34)
REG32(DPORT_APPCPU_BOOT_ADDR, 0x38)

REG32(DPORT_PRO_CACHE_CTRL, 0x40)
    FIELD(DPORT_PRO_CACHE_CTRL, CACHE_FLUSH_DONE, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL, CACHE_FLUSH_ENA, 4, 1)
    FIELD(DPORT_PRO_CACHE_CTRL, CACHE_ENA, 3, 1)

REG32(DPORT_PRO_CACHE_CTRL1, 0x44)
    FIELD(DPORT_PRO_CACHE_CTRL1, MMU_IA_CLR, 13, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_OPSDRAM, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_DROM0, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_DRAM1, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_IROM0, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_IRAM1, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_IRAM0, 5, 1)

REG32(DPORT_APP_CACHE_CTRL, 0x58)
    FIELD(DPORT_APP_CACHE_CTRL, CACHE_FLUSH_DONE, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL, CACHE_FLUSH_ENA, 4, 1)
    FIELD(DPORT_APP_CACHE_CTRL, CACHE_ENA, 3, 1)

REG32(DPORT_APP_CACHE_CTRL1, 0x5C)
    FIELD(DPORT_APP_CACHE_CTRL1, MMU_IA_CLR, 13, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_OPSDRAM, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_DROM0, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_DRAM1, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_IROM0, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_IRAM1, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_IRAM0, 5, 1)

REG32(DPORT_CPU_INTR_FROM_CPU_0, 0xdc)
REG32(DPORT_CPU_INTR_FROM_CPU_1, 0xe0)
REG32(DPORT_CPU_INTR_FROM_CPU_2, 0xe4)
REG32(DPORT_CPU_INTR_FROM_CPU_3, 0xe8)

REG32(DPORT_PRO_MAC_INTR_MAP, 0x104)
REG32(DPORT_APP_MAC_INTR_MAP, 0x218)


#define ESP32_DPORT_PRO_INTMATRIX_BASE    A_DPORT_PRO_MAC_INTR_MAP
#define ESP32_DPORT_APP_INTMATRIX_BASE    A_DPORT_APP_MAC_INTR_MAP
#define ESP32_DPORT_CROSSCORE_INT_BASE    A_DPORT_CPU_INTR_FROM_CPU_0


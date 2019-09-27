#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"
#include "sysemu/block-backend.h"
#include "hw/misc/esp32_flash_enc.h"

typedef struct Esp32DportState Esp32DportState;
typedef struct Esp32CacheState Esp32CacheState;

#define TYPE_ESP32_DPORT "misc.esp32.dport"
#define ESP32_DPORT(obj) OBJECT_CHECK(Esp32DportState, (obj), TYPE_ESP32_DPORT)

#define ESP32_CACHE_PAGE_SIZE           0x10000
#define ESP32_CACHE_PAGES_PER_REGION    64
#define ESP32_CACHE_REGION_SIZE         (ESP32_CACHE_PAGE_SIZE * ESP32_CACHE_PAGES_PER_REGION)
#define ESP32_CACHE_MMU_INVALID_VAL     0x100
#define ESP32_CACHE_MMU_ENTRY_CHANGED   0x200     /* not a hardware flag; used here to check if the page data needs to be updated */
#define ESP32_CACHE_MAX_PHYS_PAGES      0x100

typedef enum Esp32CacheRegionType {
    ESP32_DCACHE_FLASH,
    ESP32_ICACHE_FLASH,
    ESP32_DCACHE_PSRAM,
} Esp32CacheRegionType;

typedef struct Esp32CacheRegionState {
    Esp32CacheState* cache;
    MemoryRegion mem;
    MemoryRegion illegal_access_trap_mem;
    Esp32CacheRegionType type;
    hwaddr base;
    uint32_t illegal_access_retval;
    bool illegal_access_trap_en;
    bool illegal_access_status;
    uint16_t mmu_table[ESP32_CACHE_PAGES_PER_REGION];
} Esp32CacheRegionState;

typedef struct Esp32CacheState {
    Esp32DportState* dport;
    int core_id;

    uint32_t cache_ctrl_reg;
    uint32_t cache_ctrl1_reg;
    /* Using only the first 4MB range.
     * TODO: add memory regions for other ports: iram1, irom0
     */
    Esp32CacheRegionState iram0;
    Esp32CacheRegionState drom0;
    Esp32CacheRegionState dram1;  /* PSRAM */
} Esp32CacheState;

typedef struct Esp32DportState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    bool has_psram;
    int cpu_count;
    Esp32CacheState cache_state[ESP32_CPU_COUNT];
    BlockBackend *flash_blk;
    qemu_irq appcpu_stall_req;
    qemu_irq appcpu_reset_req;
    qemu_irq clk_update_req;
    qemu_irq cache_ill_irq;
    qemu_irq flash_enc_en_gpio;
    qemu_irq flash_dec_en_gpio;

    bool appcpu_reset_state;
    bool appcpu_stall_state;
    bool appcpu_clkgate_state;
    uint32_t appcpu_boot_addr;
    uint32_t cpuperiod_sel;
    uint32_t cache_ill_trap_en_reg;
    uint32_t slave_spi_config_reg;

} Esp32DportState;

void esp32_dport_clear_ill_trap_state(Esp32DportState* s);

#define ESP32_DPORT_APPCPU_STALL_GPIO   "appcpu-stall"
#define ESP32_DPORT_APPCPU_RESET_GPIO   "appcpu-reset"
#define ESP32_DPORT_CLK_UPDATE_GPIO     "clk-update"
#define ESP32_DPORT_CACHE_ILL_IRQ_GPIO  "cache-ill-irq"
#define ESP32_DPORT_FLASH_ENC_EN_GPIO   "flash-enc-en"
#define ESP32_DPORT_FLASH_DEC_EN_GPIO   "flash-dec-en"


REG32(DPORT_APPCPU_RESET, 0x2c)
REG32(DPORT_APPCPU_CLK, 0x30)
REG32(DPORT_APPCPU_RUNSTALL, 0x34)
REG32(DPORT_APPCPU_BOOT_ADDR, 0x38)

REG32(DPORT_CPU_PER_CONF, 0x3c)
    FIELD(DPORT_CPU_PER_CONF, CPUPERIOD_SEL, 0, 2)

REG32(DPORT_PRO_CACHE_CTRL, 0x40)
    FIELD(DPORT_PRO_CACHE_CTRL, CACHE_FLUSH_DONE, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL, CACHE_FLUSH_ENA, 4, 1)
    FIELD(DPORT_PRO_CACHE_CTRL, CACHE_ENA, 3, 1)

REG32(DPORT_PRO_CACHE_CTRL1, 0x44)
    FIELD(DPORT_PRO_CACHE_CTRL1, MMU_IA_CLR, 13, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_OPSDRAM, 5, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_DROM0, 4, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_DRAM1, 3, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_IROM0, 2, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_IRAM1, 1, 1)
    FIELD(DPORT_PRO_CACHE_CTRL1, MASK_IRAM0, 0, 1)

REG32(DPORT_APP_CACHE_CTRL, 0x58)
    FIELD(DPORT_APP_CACHE_CTRL, CACHE_FLUSH_DONE, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL, CACHE_FLUSH_ENA, 4, 1)
    FIELD(DPORT_APP_CACHE_CTRL, CACHE_ENA, 3, 1)

REG32(DPORT_APP_CACHE_CTRL1, 0x5C)
    FIELD(DPORT_APP_CACHE_CTRL1, MMU_IA_CLR, 13, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_OPSDRAM, 5, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_DROM0, 4, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_DRAM1, 3, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_IROM0, 2, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_IRAM1, 1, 1)
    FIELD(DPORT_APP_CACHE_CTRL1, MASK_IRAM0, 0, 1)

REG32(DPORT_SLAVE_SPI_CONFIG, 0xC8)
    FIELD(DPORT_SLAVE_SPI_CONFIG, SLAVE_SPI_ENCRYPT_ENABLE, 8, 1)
    FIELD(DPORT_SLAVE_SPI_CONFIG, SLAVE_SPI_DECRYPT_ENABLE, 12, 1)

REG32(DPORT_CPU_INTR_FROM_CPU_0, 0xdc)
REG32(DPORT_CPU_INTR_FROM_CPU_1, 0xe0)
REG32(DPORT_CPU_INTR_FROM_CPU_2, 0xe4)
REG32(DPORT_CPU_INTR_FROM_CPU_3, 0xe8)

REG32(DPORT_PRO_MAC_INTR_MAP, 0x104)
REG32(DPORT_APP_MAC_INTR_MAP, 0x218)

REG32(DPORT_PRO_DCACHE_DBUG0, 0x3f0)
    FIELD(DPORT_PRO_DCACHE_DBUG0, CACHE_STATE, 7, 12)

REG32(DPORT_PRO_DCACHE_DBUG3, 0x3FC)
    FIELD(DPORT_PRO_DCACHE_DBUG3, IA_INT_OPPOSITE, 9, 1)
    FIELD(DPORT_PRO_DCACHE_DBUG3, IA_INT_DRAM1, 10, 1)
    FIELD(DPORT_PRO_DCACHE_DBUG3, IA_INT_IROM0, 11, 1)
    FIELD(DPORT_PRO_DCACHE_DBUG3, IA_INT_IRAM1, 12, 1)
    FIELD(DPORT_PRO_DCACHE_DBUG3, IA_INT_IRAM0, 13, 1)
    FIELD(DPORT_PRO_DCACHE_DBUG3, IA_INT_DROM0, 14, 1)

REG32(DPORT_APP_DCACHE_DBUG0, 0x418)
    FIELD(DPORT_APP_DCACHE_DBUG0, CACHE_STATE, 7, 12)

REG32(DPORT_APP_DCACHE_DBUG3, 0x424)
    FIELD(DPORT_APP_DCACHE_DBUG3, IA_INT_OPPOSITE, 9, 1)
    FIELD(DPORT_APP_DCACHE_DBUG3, IA_INT_DRAM1, 10, 1)
    FIELD(DPORT_APP_DCACHE_DBUG3, IA_INT_IROM0, 11, 1)
    FIELD(DPORT_APP_DCACHE_DBUG3, IA_INT_IRAM1, 12, 1)
    FIELD(DPORT_APP_DCACHE_DBUG3, IA_INT_IRAM0, 13, 1)
    FIELD(DPORT_APP_DCACHE_DBUG3, IA_INT_DROM0, 14, 1)

REG32(DPORT_CACHE_IA_INT_EN, 0x5A0)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_PRO_OPPOSITE, 19, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_PRO_DRAM1, 18, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_PRO_IROM0, 17, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_PRO_IRAM1, 16, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_PRO_IRAM0, 15, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_PRO_DROM0, 14, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_APP_OPPOSITE, 5, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_APP_DRAM1, 4, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_APP_IROM0, 3, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_APP_IRAM1, 2, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_APP_IRAM0, 1, 1)
    FIELD(DPORT_CACHE_IA_INT_EN, IA_INT_APP_DROM0, 0, 1)


#define ESP32_DPORT_PRO_INTMATRIX_BASE    A_DPORT_PRO_MAC_INTR_MAP
#define ESP32_DPORT_APP_INTMATRIX_BASE    A_DPORT_APP_MAC_INTR_MAP
#define ESP32_DPORT_CROSSCORE_INT_BASE    A_DPORT_CPU_INTR_FROM_CPU_0


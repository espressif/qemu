#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"
#include "sysemu/block-backend.h"

#define TYPE_ESP32_EFUSE "nvram.esp32.efuse"
#define ESP32_EFUSE(obj) OBJECT_CHECK(Esp32EfuseState, (obj), TYPE_ESP32_EFUSE)

REG32(EFUSE_BLK0_RDATA0, 0x00)
REG32(EFUSE_BLK0_WDATA0, 0x1c)
REG32(EFUSE_BLK1_RDATA0, 0x38)
REG32(EFUSE_BLK2_RDATA0, 0x58)
REG32(EFUSE_BLK3_RDATA0, 0x78)
REG32(EFUSE_BLK1_WDATA0, 0x98)
REG32(EFUSE_BLK2_WDATA0, 0xb8)
REG32(EFUSE_BLK3_WDATA0, 0xd8)
REG32(EFUSE_CLK, 0xf8)
REG32(EFUSE_CONF, 0xfc)
    FIELD(EFUSE_CONF, OP_CODE, 0, 16)
REG32(EFUSE_STATUS, 0x100)
REG32(EFUSE_CMD, 0x104)
REG32(EFUSE_INT_RAW, 0x108)
REG32(EFUSE_INT_ST, 0x10c)
REG32(EFUSE_INT_ENA, 0x110)
REG32(EFUSE_INT_CLR, 0x114)
REG32(EFUSE_DAC_CONF, 0x118)
REG32(EFUSE_DEC_STATUS, 0x11c)
REG32(EFUSE_DATE, 0x1fc)

/* the following bit masks apply to CMD, INT_RAW, INT_ST, INT_ENA, INT_CLR */
#define EFUSE_READ 0x01
#define EFUSE_PGM 0x02

/* expected values of EFUSE_CONF OP_CODE field */
#define EFUSE_READ_OP_CODE 0x5AA5
#define EFUSE_PGM_OP_CODE 0x5A5A

#define ESP32_EFUSE_UPDATE_GPIO "efuse-update"


typedef struct Esp32EfuseRegs {
    union {
        struct {
            struct {
                uint32_t wr_dis_rd_dis : 1;
                uint32_t wr_dis_wr_dis : 1;
                uint32_t wr_dis_flash_crypt_cnt : 1;
                uint32_t wr_dis_mac_spi_config : 1;
                uint32_t wr_dis_unused1 : 1;
                uint32_t wr_dis_xpd_sdio : 1;
                uint32_t wr_dis_spi_pad_config : 1;
                uint32_t wr_dis_blk1 : 1;
                uint32_t wr_dis_blk2 : 1;
                uint32_t wr_dis_blk3 : 1;
                uint32_t wr_dis_flash_crypt_coding_scheme : 1;
                uint32_t wr_dis_unused2 : 1;
                uint32_t wr_dis_abs_done_0 : 1;
                uint32_t wr_dis_abs_done_1 : 1;
                uint32_t wr_dis_jtag_disable : 1;
                uint32_t wr_dis_console_dl_disable : 1;

                uint32_t rd_dis_blk1 : 1;
                uint32_t rd_dis_blk2 : 1;
                uint32_t rd_dis_blk3 : 1;
                uint32_t rd_dis_blk0_partial : 1;

                uint32_t flash_crypt_cnt : 7;
            } blk0_d0;
            uint32_t blk0_d1;
            uint32_t blk0_d2;
            uint32_t blk0_d3;
            uint32_t blk0_d4;
            struct {
                uint32_t blk0_d5_misc : 28;
                uint32_t flash_crypt_config : 4;
            } blk0_d5;
            struct {
                uint32_t coding_scheme : 2;
                uint32_t console_debug_dis : 1;
                uint32_t dis_sdio_host : 1;
                uint32_t abs_done_0 : 1;
                uint32_t abs_done_1 : 1;
                uint32_t dis_jtag : 1;
                uint32_t dis_dl_encrypt : 1;
                uint32_t dis_dl_decrypt : 1;
                uint32_t dis_dl_cache : 1;
                uint32_t key_status : 1;
            } blk0_d6;
        };
        uint32_t blk0[7];
    };
    uint32_t blk1[8];
    uint32_t blk2[8];
    uint32_t blk3[8];
} Esp32EfuseRegs;


typedef struct Esp32EfuseState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    BlockBackend *blk;
    QEMUTimer op_timer;
    qemu_irq efuse_update_gpio;

    Esp32EfuseRegs efuse_wr;
    Esp32EfuseRegs efuse_wr_dis;
    Esp32EfuseRegs efuse_rd;
    Esp32EfuseRegs efuse_rd_dis;

    uint32_t clk_reg;
    uint32_t conf_reg;
    uint32_t status_reg;
    uint32_t cmd_reg;
    uint32_t int_raw_reg;
    uint32_t int_st_reg;
    uint32_t int_ena_reg;
    uint32_t dac_conf_reg;
} Esp32EfuseState;

/* returns NULL unless there is exactly one device */
static inline Esp32EfuseState *esp32_efuse_find(void)
{
    Object *o = object_resolve_path_type("", TYPE_ESP32_EFUSE, NULL);

    return o ? ESP32_EFUSE(o) : NULL;
}

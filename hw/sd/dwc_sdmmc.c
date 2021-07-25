/*
 * Designware SD/MMC controller emulation
 *
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co. Ltd.
 *
 * Based on Allwinner SD Host controller emulation (allwinner-sdhost.c),
 *   Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/sd/dwc_sdmmc.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "sysemu/blockdev.h"
#include "sysemu/dma.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qom/object.h"
#include "hw/registerfields.h"

/* This implementation is simplified:
 * - Only DMA mode is implemented, with 32-bit DMA descriptor pointers.
 * - FIFO is not implemented.
 * - No error interrupts or conditions are generated.
 * - Boot mode is not implemented.
 * - Clock settings are ignored.
 *
 * The hardware seems to work in a manner very similar to Allwinner SD Host controller (allwinner-sdhost.c),
 * notable exceptions being:
 * - slightly different IDMAC descriptor structure
 * - slightly different interrupt handling logic
 * It is possible that Allwinner SD Host is based on an earlier version of Designware SD/MMC IP,
 * or is heavily inspired by it.
 */

#ifdef DWC_SDMMC_DEBUG
#define DEBUG(...) qemu_log(__VA_ARGS__)
#else
#define DEBUG(...)
#endif

typedef struct {
    uint32_t reserved1: 1;
    uint32_t disable_int_on_completion: 1;
    uint32_t last_descriptor: 1;
    uint32_t first_descriptor: 1;
    uint32_t second_address_chained: 1;
    uint32_t end_of_ring: 1;
    uint32_t reserved2: 24;
    uint32_t card_error_summary: 1;
    uint32_t owned_by_idmac: 1;
    uint32_t buffer1_size: 13;
    uint32_t buffer2_size: 13;
    uint32_t reserved3: 6;
    uint32_t buffer1_ptr;
    union {
        uint32_t buffer2_ptr;
        uint32_t next_desc_ptr;
    };
} sdmmc_desc_t;

typedef struct {
    uint32_t cmd_index: 6;          ///< Command index
    uint32_t response_expect: 1;    ///< set if response is expected
    uint32_t response_long: 1;      ///< 0: short response expected, 1: long response expected
    uint32_t check_response_crc: 1; ///< set if controller should check response CRC
    uint32_t data_expected: 1;      ///< 0: no data expected, 1: data expected
    uint32_t rw: 1;                 ///< 0: read from card, 1: write to card (don't care if no data expected)
    uint32_t stream_mode: 1;        ///< 0: block transfer, 1: stream transfer (don't care if no data expected)
    uint32_t send_auto_stop: 1;     ///< set to send stop at the end of the transfer
    uint32_t wait_complete: 1;      ///< 0: send command at once, 1: wait for previous command to complete
    uint32_t stop_abort_cmd: 1;     ///< set if this is a stop or abort command intended to stop current transfer
    uint32_t send_init: 1;          ///< set to send init sequence (80 clocks of 1)
    uint32_t card_num: 5;           ///< card number
    uint32_t update_clk_reg: 1;     ///< 0: normal command, 1: don't send command, just update clock registers
    uint32_t read_ceata: 1;         ///< set if performing read from CE-ATA device
    uint32_t ccs_expected: 1;       ///< set if CCS is expected from CE-ATA device
    uint32_t enable_boot: 1;        ///< set for mandatory boot mode
    uint32_t expect_boot_ack: 1;    ///< when set along with enable_boot, controller expects boot ack pattern
    uint32_t disable_boot: 1;       ///< set to terminate boot operation (don't set along with enable_boot)
    uint32_t boot_mode: 1;          ///< 0: mandatory boot operation, 1: alternate boot operation
    uint32_t volt_switch: 1;        ///< set to enable voltage switching (for CMD11 only)
    uint32_t use_hold_reg: 1;       ///< clear to bypass HOLD register
    uint32_t reserved: 1;
    uint32_t start_command: 1;      ///< Start command; once command is sent to the card, bit is cleared.
} sdmmc_hw_cmd_t;  ///< command format used in cmd register; this structure is defined to make it easier to build command values

_Static_assert(sizeof(sdmmc_hw_cmd_t) == 4, "invalid size of sdmmc_cmd_t structure");


REG32(SDMMC_CTRL, 0x00)
    FIELD(SDMMC_CTRL, RST, 0, 1);
    FIELD(SDMMC_CTRL, FIFO_RST, 1, 1);
    FIELD(SDMMC_CTRL, DMA_RST, 2, 1);
    FIELD(SDMMC_CTRL, INTEN, 4, 1);
    FIELD(SDMMC_CTRL, DMAEN, 5, 1);

REG32(SDMMC_PWREN, 0x04)
REG32(SDMMC_CLKDIV, 0x08)
REG32(SDMMC_CLKSRC, 0x0c)
REG32(SDMMC_CLKENA, 0x10)
REG32(SDMMC_TMOUT, 0x14)
REG32(SDMMC_CTYPE, 0x18)
REG32(SDMMC_BLKSIZ, 0x1c)
REG32(SDMMC_BYTCNT, 0x20)
REG32(SDMMC_INTMASK, 0x24)
REG32(SDMMC_CMDARG, 0x28)
REG32(SDMMC_CMD, 0x2c)
    FIELD(SDMMC_CMD, START, 31, 1);

REG32(SDMMC_RESP0, 0x30)
REG32(SDMMC_RESP1, 0x34)
REG32(SDMMC_RESP2, 0x38)
REG32(SDMMC_RESP3, 0x3c)

REG32(SDMMC_MINTSTS, 0x40)
REG32(SDMMC_RINTSTS, 0x44)
REG32(SDMMC_STATUS, 0x48)
REG32(SDMMC_FIFOTH, 0x4c)
REG32(SDMMC_CDETECT, 0x50)
REG32(SDMMC_WRTPRT, 0x54)
REG32(SDMMC_GPIO, 0x58)
REG32(SDMMC_TCBCNT, 0x5c)
REG32(SDMMC_TBBCNT, 0x60)
REG32(SDMMC_DEBNCE, 0x64)
REG32(SDMMC_USRID, 0x68)
REG32(SDMMC_VERID, 0x6c)
REG32(SDMMC_HCON, 0x70)
REG32(SDMMC_UHS_REG, 0x74)
REG32(SDMMC_RST_N, 0x78)
REG32(SDMMC_BMOD, 0x80)
REG32(SDMMC_PLDMND, 0x84)
REG32(SDMMC_DBADDR, 0x88)
REG32(SDMMC_DBADDRU, 0x8c)
REG32(SDMMC_IDSTS, 0x8c)
REG32(SDMMC_IDINTEN, 0x90)
REG32(SDMMC_DSCADDR, 0x94)
REG32(SDMMC_DSCADDRL, 0x98)
REG32(SDMMC_DSCADDRU, 0x9c)
REG32(SDMMC_BUFADDRL, 0xa0)
REG32(SDMMC_BUFADDRU, 0xa4)
REG32(SDMMC_CARDTHRCTL, 0x100)
REG32(SDMMC_BACK_END_POWER, 0x104)
REG32(SDMMC_UHS_REG_EXT, 0x108)
REG32(SDMMC_EMMC_DDR_REG, 0x10c)
REG32(SDMMC_ENABLE_SHIFT, 0x110)

#define SDMMC_INTMASK_IO_SLOT1  BIT(17)
#define SDMMC_INTMASK_IO_SLOT0  BIT(16)
#define SDMMC_INTMASK_EBE       BIT(15)
#define SDMMC_INTMASK_ACD       BIT(14)
#define SDMMC_INTMASK_SBE       BIT(13)
#define SDMMC_INTMASK_HLE       BIT(12)
#define SDMMC_INTMASK_FRUN      BIT(11)
#define SDMMC_INTMASK_HTO       BIT(10)
#define SDMMC_INTMASK_DTO       BIT(9)
#define SDMMC_INTMASK_RTO       BIT(8)
#define SDMMC_INTMASK_DCRC      BIT(7)
#define SDMMC_INTMASK_RCRC      BIT(6)
#define SDMMC_INTMASK_RXDR      BIT(5)
#define SDMMC_INTMASK_TXDR      BIT(4)
#define SDMMC_INTMASK_DATA_OVER BIT(3)
#define SDMMC_INTMASK_CMD_DONE  BIT(2)
#define SDMMC_INTMASK_RESP_ERR  BIT(1)
#define SDMMC_INTMASK_CD        BIT(0)

#define SDMMC_IDMAC_INTMASK_AI  BIT(9)
#define SDMMC_IDMAC_INTMASK_NI  BIT(8)
#define SDMMC_IDMAC_INTMASK_CES BIT(5)
#define SDMMC_IDMAC_INTMASK_DU  BIT(4)
#define SDMMC_IDMAC_INTMASK_FBE BIT(2)
#define SDMMC_IDMAC_INTMASK_RI  BIT(1)
#define SDMMC_IDMAC_INTMASK_TI  BIT(0)


#define SDMMC_DMA_MAX_BUF_LEN 4096

static bool dwc_sdmmc_ctrl_interrupts_enabled(uint32_t ctrl_reg)
{
    return FIELD_EX32(ctrl_reg, SDMMC_CTRL, INTEN) == 1;
}

static sdmmc_hw_cmd_t dwc_sdmmc_get_hw_cmd(DWCSDMMCState *s)
{
    sdmmc_hw_cmd_t hw_cmd;
    memcpy(&hw_cmd, &s->cmd, sizeof(hw_cmd));
    return hw_cmd;
}

static void dwc_sdmmc_handle_reset(DWCSDMMCState *s)
{
    const uint32_t reset_mask = R_SDMMC_CTRL_FIFO_RST_MASK | R_SDMMC_CTRL_RST_MASK | R_SDMMC_CTRL_DMA_RST_MASK;
    if (s->ctrl & reset_mask) {
        /* just clear it */
        s->ctrl &= ~reset_mask;
    }
}

static void dwc_sdmmc_update_irq(DWCSDMMCState *s)
{
    uint32_t irq = 0;
    if (dwc_sdmmc_ctrl_interrupts_enabled(s->ctrl)) {
        irq = s->rintsts & s->intmask;
        irq |= (s->idsts & s->idinten) << 16;
    }
    DEBUG("%s: mask=0x%04x, sts=0x%04x irq=%d\n", __func__, s->intmask, s->rintsts, irq);
    qemu_set_irq(s->irq, irq);
}

static void dwc_sdmmc_handle_cmd(DWCSDMMCState *s)
{
    SDRequest request;
    uint8_t resp[16];
    int rlen;
    DEBUG("%s: hw_cmd=0x%08x\n", __func__, s->cmd);
    sdmmc_hw_cmd_t hw_cmd = dwc_sdmmc_get_hw_cmd(s);
    if (hw_cmd.update_clk_reg) {
        goto done;
    }

    request.cmd = hw_cmd.cmd_index;
    request.arg = s->cmdarg;

    rlen = sdbus_do_command(&s->sdbus, &request, resp);
    s->rintsts |= SDMMC_INTMASK_CMD_DONE;
    if (rlen < 0) {
        DEBUG("%s: error: rlen=%d\n", __func__, rlen);
    } else {
        if (hw_cmd.response_expect) {
            if (rlen == 4 && !hw_cmd.response_long) {
                s->resp[0] = ldl_be_p(&resp[0]);
                s->resp[1] = s->resp[2] = s->resp[3] = 0;

            } else if (rlen == 16 && hw_cmd.response_long) {
                s->resp[0] = ldl_be_p(&resp[12]);
                s->resp[1] = ldl_be_p(&resp[8]);
                s->resp[2] = ldl_be_p(&resp[4]);
                s->resp[3] = ldl_be_p(&resp[0]);
            } else {
                s->rintsts |= SDMMC_INTMASK_RTO;
                goto done;
            }
        }
    }

done:
    DEBUG("%s: ending with rinsts=0x%04x\n", __func__, s->rintsts);
    s->cmd = FIELD_DP32(s->cmd, SDMMC_CMD, START, 0);
    dwc_sdmmc_update_irq(s);
}

static void dwc_sdmmc_update_bytes_left(DWCSDMMCState *s, size_t bytes_done)
{
    if (s->bytes_left > bytes_done) {
        s->bytes_left -= bytes_done;
    } else {
        s->bytes_left = 0;
    }
    DEBUG("%s: bytes_left = %d\n", __func__, (unsigned) s->bytes_left);
    if (s->bytes_left == 0) {
        DEBUG("%s: bytes_left = 0, setting SDMMC_INTMASK_DATA_OVER\n", __func__);
        s->rintsts |= SDMMC_INTMASK_DATA_OVER;
    }
}

static size_t dwc_sdmmc_handle_one_desc(DWCSDMMCState *s, hwaddr desc_addr, sdmmc_desc_t *desc, bool is_write, size_t max_bytes)
{
    uint32_t num_done = 0;
    uint32_t num_bytes = max_bytes;
    uint8_t buf[4096];

    /* Read descriptor */
    dma_memory_read(&address_space_memory, desc_addr, desc, sizeof(*desc), MEMTXATTRS_UNSPECIFIED);
    if (desc->buffer1_size < num_bytes) {
        num_bytes = desc->buffer1_size;
    }

    if (desc->owned_by_idmac == 0) {
        /* ran into a descriptor owned by software */
        return 0;
    }

    while (num_done < num_bytes) {
        /* Try to completely fill the local buffer */
        uint32_t buf_bytes = num_bytes - num_done;
        if (buf_bytes > sizeof(buf)) {
            buf_bytes = sizeof(buf);
        }
        DEBUG("%s: %sing %d bytes from descr 0x%08x\n", __func__, is_write?"write":"read", buf_bytes, (uint32_t) desc_addr);
        /* Write to SD bus */
        if (is_write) {
            dma_memory_read(&address_space_memory,
                            desc->buffer1_ptr + num_done,
                            buf, buf_bytes, MEMTXATTRS_UNSPECIFIED);
            sdbus_write_data(&s->sdbus, buf, buf_bytes);

            /* Read from SD bus */
        } else {
            sdbus_read_data(&s->sdbus, buf, buf_bytes);
            dma_memory_write(&address_space_memory,
                             desc->buffer1_ptr + num_done,
                             buf, buf_bytes, MEMTXATTRS_UNSPECIFIED);
        }
        num_done += buf_bytes;
    }

    /* Clear hold flag and flush descriptor */
    sdmmc_desc_t new_desc = *desc;
    new_desc.owned_by_idmac = 0;
    dma_memory_write(&address_space_memory, desc_addr, &new_desc, sizeof(new_desc), MEMTXATTRS_UNSPECIFIED);

    /* Update DMAC bits */
    s->idsts |= is_write ? SDMMC_IDMAC_INTMASK_TI : SDMMC_IDMAC_INTMASK_RI;

    return num_done;

}

static void dwc_sdmmc_handle_dma(DWCSDMMCState *s)
{
    sdmmc_desc_t desc;
    hwaddr desc_addr = s->dscaddr;
    sdmmc_hw_cmd_t hw_cmd = dwc_sdmmc_get_hw_cmd(s);
    bool is_write = hw_cmd.rw == 1;
    uint32_t bytes_done = 0;

    if (s->bytcnt == 0 || s->blksiz == 0 ||
        !FIELD_EX32(s->ctrl, SDMMC_CTRL, DMAEN)) {
        DEBUG("%s: nothing to do (bytcnt=%d, blksiz=%d)\n", __func__, s->bytcnt, s->blksiz);
        return;
    }

    if (!is_write && !sdbus_data_ready(&s->sdbus)) {
        return;
    }

    while (s->bytcnt > 0) {
        DEBUG("%s: handling descriptor @0x%0x, bytcnt=%d\n", __func__, (uint32_t) desc_addr, s->bytcnt);
        bytes_done = dwc_sdmmc_handle_one_desc(s, desc_addr, &desc, is_write, s->bytcnt);
        dwc_sdmmc_update_bytes_left(s, bytes_done);

        if (bytes_done <= s->bytcnt) {
            s->bytcnt -= bytes_done;
        } else {
            s->bytcnt = 0;
        }

        if (desc.last_descriptor || desc.owned_by_idmac == 0) {
            break;
        } else {
            desc_addr = desc.next_desc_ptr;
            s->dscaddr = desc_addr;
        }
    }
    DEBUG("%s: finished with bytcnt=%d at dscaddr=0x%08x\n", __func__, s->bytcnt, s->dscaddr);
}

static void dwc_sdmmc_maybe_autostop(DWCSDMMCState *s)
{
    sdmmc_hw_cmd_t hw_cmd = dwc_sdmmc_get_hw_cmd(s);
    if (hw_cmd.send_auto_stop && s->bytes_left == 0) {
        /* First save current command registers */
        uint32_t saved_cmd = s->cmd;
        uint32_t saved_arg = s->cmdarg;

        /* Prepare stop command (CMD12) */
        sdmmc_hw_cmd_t auto_stop_cmd = hw_cmd;
        auto_stop_cmd.cmd_index = 12;
        memcpy(&s->cmd, &auto_stop_cmd, sizeof(s->cmd));
        s->cmdarg = 0;

        /* Put the command on SD bus */
        dwc_sdmmc_handle_cmd(s);

        /* Restore command values */
        s->cmd = saved_cmd;
        s->cmdarg = saved_arg;

        /* Set IRQ status bit for automatic stop done */
        s->rintsts |= SDMMC_INTMASK_ACD;
    }
}


static void dwc_sdmmc_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    DWCSDMMCState *s = DWC_SDMMC(opaque);

    switch (offset) {
        case A_SDMMC_CTRL:
            s->ctrl = value;
            dwc_sdmmc_handle_reset(s);
            dwc_sdmmc_update_irq(s);
            break;

        case A_SDMMC_CMD: {
            s->cmd = value;
            if (FIELD_EX32(value, SDMMC_CMD, START)) {
                dwc_sdmmc_handle_cmd(s);
                dwc_sdmmc_handle_dma(s);
                dwc_sdmmc_maybe_autostop(s);
                dwc_sdmmc_update_irq(s);
            }
            break;
        }
        case A_SDMMC_PLDMND:
            dwc_sdmmc_handle_dma(s);
            dwc_sdmmc_maybe_autostop(s);
            dwc_sdmmc_update_irq(s);
            break;

        case A_SDMMC_CMDARG:
            s->cmdarg = value;
            break;

        case A_SDMMC_INTMASK:
            s->intmask = value;
            dwc_sdmmc_update_irq(s);
            break;

        case A_SDMMC_RINTSTS:
            s->rintsts &= ~value;
            dwc_sdmmc_update_irq(s);
            break;

        case A_SDMMC_IDSTS:
            s->idsts &= ~value;
            dwc_sdmmc_update_irq(s);
            break;

        case A_SDMMC_IDINTEN:
            s->idinten = value;
            dwc_sdmmc_update_irq(s);
            break;

        case A_SDMMC_DBADDR:
            s->dbaddr = value;
            s->dscaddr = value;
            break;

        case A_SDMMC_BYTCNT:
            s->bytcnt = value;
            s->bytes_left = value;
            break;

        case A_SDMMC_BLKSIZ:
            s->blksiz = value;
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "%s: write@0x%02x value=0x%08x\n", __func__, (uint32_t) offset, (uint32_t) value);
    }

}

static uint64_t dwc_sdmmc_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    DWCSDMMCState *s = DWC_SDMMC(opaque);
    switch (offset) {
        case A_SDMMC_CTRL:
            return s->ctrl;
        case A_SDMMC_INTMASK:
            return s->intmask;
        case A_SDMMC_CMD:
            return s->cmd;
        case A_SDMMC_CMDARG:
            return s->cmdarg;
        case A_SDMMC_RESP0 ... A_SDMMC_RESP3:
            return s->resp[(offset - A_SDMMC_RESP0) / 4];
        case A_SDMMC_RINTSTS:
            return s->rintsts;
        case A_SDMMC_MINTSTS:
            return s->intmask & s->rintsts;
        case A_SDMMC_IDSTS:
            return s->idsts;
        case A_SDMMC_IDINTEN:
            return s->idinten;
        case A_SDMMC_BLKSIZ:
            return s->blksiz;
        case A_SDMMC_BYTCNT:
            return s->bytcnt;
        case A_SDMMC_DBADDR:
            return s->dbaddr;
        case A_SDMMC_DSCADDR:
            return s->dscaddr;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: read@0x%02x\n", __func__, (uint32_t) offset);
    }
    return 0;
}


static const MemoryRegionOps dwc_sdmmc_ops = {
    .read = dwc_sdmmc_read,
    .write = dwc_sdmmc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};



static Property dwc_sdmmc_properties[] = {
        DEFINE_PROP_END_OF_LIST(),
};

static void dwc_sdmmc_init(Object *obj)
{
    DWCSDMMCState *s = DWC_SDMMC(obj);

    qbus_init(&s->sdbus, sizeof(s->sdbus),
                        TYPE_SD_BUS, DEVICE(s), "sd-bus");

    memory_region_init_io(&s->iomem, obj, &dwc_sdmmc_ops, s,
                          TYPE_DWC_SDMMC, 4 * KiB);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
}

static void dwc_sdmmc_reset(DeviceState *dev)
{
    DWCSDMMCState *s = DWC_SDMMC(dev);
    s->cmd = 0;
    s->cmdarg = 0;
    s->intmask = 0;
    s->rintsts = 0;
    s->ctrl = 0;
    s->bytes_left = 0;
    s->idinten = 0;
    s->idsts = 0;
    s->blksiz = 512;
    s->bytcnt = 0;
    s->dbaddr = 0;
    s->pldmnd = 0;
    memset(s->resp, 0, sizeof(s->resp));
}

static void dwc_sdmmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = dwc_sdmmc_reset;
    device_class_set_props(dc, dwc_sdmmc_properties);
}

static TypeInfo dwc_sdmmc_info = {
    .name          = TYPE_DWC_SDMMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = dwc_sdmmc_init,
    .instance_size = sizeof(DWCSDMMCState),
    .class_init    = dwc_sdmmc_class_init,
};

static void dwc_sdmmc_register_types(void)
{
    type_register_static(&dwc_sdmmc_info);
}

type_init(dwc_sdmmc_register_types)

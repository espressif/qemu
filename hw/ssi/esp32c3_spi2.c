/*
 * ESP32-C3 GPSPI2 (general-purpose SPI2) controller
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "hw/ssi/esp32c3_spi2.h"
#include "qemu/error-report.h"

#define SPI2_DEBUG      0
#define SPI2_WARNING    1


static void esp32c3_spi2_update_irq(ESP32C3Spi2State *s)
{
    /*
     * For polling mode: DMA_INT_ST should reflect DMA_INT_RAW directly
     * so that software can poll for TRANS_DONE without enabling interrupts.
     * The actual IRQ only fires when the corresponding bit is also enabled
     * in DMA_INT_ENA.
     */
    s->dma_int_st = s->dma_int_raw;

    /* Only raise IRQ if corresponding bits are enabled */
    uint32_t irq_pending = s->dma_int_raw & s->dma_int_ena;
#if SPI2_DEBUG
    static uint32_t last_irq_state = 0;
    if (irq_pending != last_irq_state) {
        warn_report("[SPI2] IRQ: raw=0x%x ena=0x%x pending=0x%x -> %s",
                    s->dma_int_raw, s->dma_int_ena, irq_pending,
                    irq_pending ? "RAISE" : "LOWER");
        last_irq_state = irq_pending;
    }
#endif
    if (irq_pending) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}


static uint64_t esp32c3_spi2_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3Spi2State *s = ESP32C3_SPI2(opaque);

    uint64_t r = 0;
    switch (addr) {
        case A_GPSPI2_CMD:
            r = s->cmd;
            break;
        case A_GPSPI2_ADDR:
            r = s->addr;
            break;
        case A_GPSPI2_CTRL:
            r = s->ctrl;
            break;
        case A_GPSPI2_CLOCK:
            r = s->clock;
            break;
        case A_GPSPI2_USER:
            r = s->user;
            break;
        case A_GPSPI2_USER1:
            r = s->user1;
            break;
        case A_GPSPI2_USER2:
            r = s->user2;
            break;
        case A_GPSPI2_MS_DLEN:
            r = s->ms_dlen;
            break;
        case A_GPSPI2_MISC:
            r = s->misc;
            break;
        case A_GPSPI2_DMA_CONF:
            r = s->dma_conf;
            break;
        case A_GPSPI2_DMA_INT_ENA:
            r = s->dma_int_ena;
            break;
        case A_GPSPI2_DMA_INT_CLR:
            r = 0;
            break;
        case A_GPSPI2_DMA_INT_RAW:
            r = s->dma_int_raw;
            break;
        case A_GPSPI2_DMA_INT_ST:
            r = s->dma_int_st;
            break;
        case A_GPSPI2_W0 ... A_GPSPI2_W15:
            r = s->data_reg[(addr - A_GPSPI2_W0) / sizeof(uint32_t)];
#if SPI2_DEBUG
            info_report("[SPI2] READ W%lu = 0x%08lx",
                        (addr - A_GPSPI2_W0) / sizeof(uint32_t), r);
#endif
            break;
        /* QEMU extension: additional W registers for large CPU-mode transfers */
        case GPSPI2_W_EXT_BASE ... (GPSPI2_W_EXT_END - 1): {
            uint32_t idx = 16 + (addr - GPSPI2_W_EXT_BASE) / sizeof(uint32_t);
            if (idx < ESP32C3_SPI2_BUF_WORDS) {
                r = s->data_reg[idx];
            }
            break;
        }
        case A_GPSPI2_SLAVE:
            r = s->slave;
            break;
        case A_GPSPI2_CLK_GATE:
            r = s->clk_gate;
            break;
        case A_GPSPI2_DATE:
            r = s->date;
            break;
        case 0x24: /* SPI_DIN_MODE_REG */
        case 0x28: /* SPI_DIN_NUM_REG */
        case 0x2C: /* SPI_DOUT_MODE_REG */
        case 0x44: /* Additional registers after DMA_INT_ST */
        case 0x48:
        case 0x4C:
        case 0x50:
        case 0x54:
        case 0x58:
        case 0x5C:
        case 0x60:
        case 0x64:
        case 0x68:
        case 0x6C:
        case 0x70:
        case 0x74:
        case 0x78:
        case 0x7C:
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8C:
        case 0x90:
        case 0x94:
        case 0xD8: /* After W15 */
        case 0xDC:
        case 0xE4: /* SPI_SLAVE1_REG */
            r = 0;
            break;
        default:
#if SPI2_DEBUG
            warn_report("[SPI2] Unsupported read from 0x%lx", addr);
#endif
            break;
    }

#if SPI2_DEBUG
    if (addr == A_GPSPI2_CMD || addr == A_GPSPI2_DMA_INT_ST || addr == A_GPSPI2_DMA_INT_RAW) {
        info_report("[SPI2] READ 0x%lx = 0x%08lx", addr, r);
    }
#endif

    return r;
}


/**
 * @brief Perform a user-mode SPI transaction on GPSPI2.
 *
 * This is triggered when bit 24 (SPI_USR) is set in the CMD register.
 * The transaction phases (command, address, dummy, MOSI, MISO) are controlled
 * by the USER register bits.
 */
static void esp32c3_spi2_begin_transaction(ESP32C3Spi2State *s)
{
    uint8_t *buf = (uint8_t *)s->data_reg;

    /* Determine which CS line to assert. Default to CS0. */
    int cs_line = 0;

    /* Check if this CS line is disabled (software-controlled via GPIO).
     * When CSx_DIS is set, the hardware CS is disabled and we shouldn't
     * toggle it. The actual CS is controlled via GPIO separately.
     * However, for QEMU SD card emulation, we need to keep CS asserted
     * during the transaction. */
    uint32_t cs_dis_mask = 1 << cs_line;  /* CS0_DIS=bit0, CS1_DIS=bit1, CS2_DIS=bit2 */
    bool cs_disabled = (s->misc & cs_dis_mask) != 0;

    /* Assert CS low - but only if not already low.
     * Avoid spurious CS edges which would reset the SSI peripheral state. */
    if (s->cs_state[cs_line] != 0) {
#if SPI2_DEBUG
        info_report("[SPI2] CS%d -> LOW (misc=0x%08x, cs_disabled=%d, cs_keep_active=%d)",
                    cs_line, s->misc, cs_disabled,
                    !!(s->misc & R_GPSPI2_MISC_CS_KEEP_ACTIVE_MASK));
#endif
        qemu_set_irq(s->cs_gpio[cs_line], 0);
        s->cs_state[cs_line] = 0;
    }

    /* Command phase */
    if (s->user & R_GPSPI2_USER_USR_COMMAND_MASK) {
        uint32_t cmd_bitlen = FIELD_EX32(s->user2, GPSPI2_USER2, USR_COMMAND_BITLEN);
        uint32_t cmd_bytes = (cmd_bitlen + 1) / 8;
        uint32_t cmd_value = FIELD_EX32(s->user2, GPSPI2_USER2, USR_COMMAND_VALUE);

#if SPI2_DEBUG
        info_report("[SPI2] TX CMD: val=0x%x bytes=%u", cmd_value, cmd_bytes);
#endif
        /* Send command bytes, MSB first */
        for (int i = cmd_bytes - 1; i >= 0; i--) {
            ssi_transfer(s->spi, (cmd_value >> (i * 8)) & 0xFF);
        }
    }

    /* Address phase */
    if (s->user & R_GPSPI2_USER_USR_ADDR_MASK) {
        uint32_t addr_bitlen = FIELD_EX32(s->user1, GPSPI2_USER1, USR_ADDR_BITLEN);
        uint32_t addr_bytes = (addr_bitlen + 1) / 8;

        /* SPI expects address MSB first; byte-swap from little-endian storage */
        uint32_t addr_be = bswap32(s->addr);

        /* Shift so that the relevant bytes are at the beginning */
        if (addr_bytes > 0 && addr_bytes <= 4) {
            addr_be = addr_be >> (32 - addr_bytes * 8);
        }

#if SPI2_DEBUG
        info_report("[SPI2] TX ADDR: 0x%x bytes=%u", s->addr, addr_bytes);
#endif
        uint8_t *addr_ptr = (uint8_t *)&addr_be;
        for (uint32_t i = 0; i < addr_bytes; i++) {
            ssi_transfer(s->spi, addr_ptr[i]);
        }
    }

    /* Dummy phase */
    if (s->user & R_GPSPI2_USER_USR_DUMMY_MASK) {
        uint32_t dummy_cyclelen = FIELD_EX32(s->user1, GPSPI2_USER1, USR_DUMMY_CYCLELEN);
        /* dummy_cyclelen is (cycles - 1), so actual cycles = dummy_cyclelen + 1.
         * Each cycle is one bit on the SPI bus, so convert to bytes rounding up. */
        uint32_t dummy_bytes = (dummy_cyclelen + 1 + 7) / 8;
#if SPI2_DEBUG
        info_report("[SPI2] DUMMY: %u bytes", dummy_bytes);
#endif
        for (uint32_t i = 0; i < dummy_bytes; i++) {
            ssi_transfer(s->spi, 0x00);
        }
    }

    /* Data phase: MS_DLEN contains (bit_count - 1) in bits 17:0 */
    uint32_t data_bitlen = FIELD_EX32(s->ms_dlen, GPSPI2_MS_DLEN, MS_DATA_BITLEN);
    uint32_t data_len = (data_bitlen + 1) / 8;

    /* Clamp to the size of our data register buffer.
     * For QEMU, buffer is larger than real hardware to support SD card reads */
    if (data_len > ESP32C3_SPI2_BUF_WORDS * 4) {
        warn_report("[SPI2] Transaction %u bytes exceeds buffer %u bytes, clamping",
                    data_len, ESP32C3_SPI2_BUF_WORDS * 4);
        data_len = ESP32C3_SPI2_BUF_WORDS * 4;
    }

    bool do_mosi = (s->user & R_GPSPI2_USER_USR_MOSI_MASK) != 0;
    bool do_miso = (s->user & R_GPSPI2_USER_USR_MISO_MASK) != 0;

#if SPI2_DEBUG
    info_report("[SPI2] DATA: len=%u mosi=%d miso=%d user=0x%08x ms_dlen=0x%x",
                data_len, do_mosi, do_miso, s->user, s->ms_dlen);
#endif

    if (do_mosi && do_miso) {
        /* Full-duplex: send from data_reg and receive into data_reg simultaneously */
        for (uint32_t i = 0; i < data_len; i++) {
            uint8_t tx = buf[i];
            uint32_t res = ssi_transfer(s->spi, tx);
            buf[i] = (uint8_t)res;
#if SPI2_DEBUG
            if (i < 20) info_report("[SPI2]   FD[%u]: tx=0x%02x rx=0x%02x", i, tx, res & 0xff);
#endif
        }
    } else if (do_mosi) {
        /* TX only: send bytes from data_reg */
        for (uint32_t i = 0; i < data_len; i++) {
            ssi_transfer(s->spi, buf[i]);
        }
#if SPI2_DEBUG
        info_report("[SPI2]   TX: first bytes: %02x %02x %02x %02x",
                    data_len > 0 ? buf[0] : 0, data_len > 1 ? buf[1] : 0,
                    data_len > 2 ? buf[2] : 0, data_len > 3 ? buf[3] : 0);
#endif
    } else if (do_miso) {
        /* RX only: receive bytes into data_reg (send 0xFF dummy bytes) */
        for (uint32_t i = 0; i < data_len; i++) {
            uint32_t res = ssi_transfer(s->spi, 0xFF);
            buf[i] = (uint8_t)res;
        }
#if SPI2_DEBUG
        info_report("[SPI2]   RX: first bytes: %02x %02x %02x %02x",
                    data_len > 0 ? buf[0] : 0, data_len > 1 ? buf[1] : 0,
                    data_len > 2 ? buf[2] : 0, data_len > 3 ? buf[3] : 0);
#endif
    }

    /* Deassert CS high, unless:
     * 1. CS_KEEP_ACTIVE is set in MISC register, OR
     * 2. CSx_DIS is set (software-controlled CS via GPIO).
     *
     * When CSx_DIS is set, the actual CS is controlled via GPIO, not the SPI
     * peripheral. For QEMU SD card emulation, we keep CS low because the
     * ESP-IDF SDSPI driver does multiple single-byte SPI transfers within
     * one "CS-asserted" session controlled via GPIO. The ssi-sd state machine
     * needs CS to stay low to properly accumulate command bytes across
     * multiple SPI transfers. */
    if (!(s->misc & R_GPSPI2_MISC_CS_KEEP_ACTIVE_MASK) && !cs_disabled) {
        if (s->cs_state[cs_line] != 1) {
#if SPI2_DEBUG
            info_report("[SPI2] CS%d -> HIGH (cs_keep_active=0, cs_disabled=0)", cs_line);
#endif
            qemu_set_irq(s->cs_gpio[cs_line], 1);
            s->cs_state[cs_line] = 1;
        }
    }
#if SPI2_DEBUG
    else {
        /* CS stays low, no need to log every time */
    }
#endif

    /* Clear the USR bit in CMD to indicate transaction complete */
    s->cmd &= ~R_GPSPI2_CMD_USR_MASK;

    /* Set TRANS_DONE in DMA_INT_RAW */
    s->dma_int_raw |= R_GPSPI2_DMA_INT_RAW_TRANS_DONE_MASK;

    /* Update interrupt status and possibly raise IRQ */
    esp32c3_spi2_update_irq(s);
}


/**
 * @brief Determine if DMA mode should be used for the transaction.
 *
 * DMA mode is used when:
 * 1. GDMA controller is available (linked by machine)
 * 2. A GDMA channel is configured for SPI2 (PERI_SEL = GDMA_SPI2)
 */
static bool esp32c3_spi2_use_dma_mode(ESP32C3Spi2State *s)
{
    /* TEMPORARY: Force CPU mode for QEMU SD card testing.
     * The GDMA integration needs more work to properly support SPI DMA.
     * For now, using CPU mode (W0-W15 data registers) works correctly. */
    (void)s;
    return false;

#if 0  /* Disabled until GDMA integration is complete */
    uint32_t dummy;

    fprintf(stderr, "[SPI2] use_dma_mode: s->gdma=%p\n", (void*)s->gdma);
    fflush(stderr);

    /* No GDMA controller linked, must use CPU mode */
    if (s->gdma == NULL) {
        fprintf(stderr, "[SPI2] use_dma_mode: GDMA is NULL, using CPU mode\n");
        fflush(stderr);
        return false;
    }

    /* Check if any GDMA channel is configured for SPI2 */
    bool have_out = esp_gdma_get_channel_periph(s->gdma, GDMA_SPI2, ESP_GDMA_OUT_IDX, &dummy);
    bool have_in = esp_gdma_get_channel_periph(s->gdma, GDMA_SPI2, ESP_GDMA_IN_IDX, &dummy);

    /* Use DMA mode if at least one channel is configured */
    return have_out || have_in;
#endif
}


/**
 * @brief Perform a DMA-mode SPI transaction on GPSPI2.
 *
 * This reads TX data from GDMA OUT channel, sends via SPI bus,
 * receives data, and writes to GDMA IN channel.
 */
static void esp32c3_spi2_dma_transaction(ESP32C3Spi2State *s)
{
    uint32_t gdma_out_idx = 0;
    uint32_t gdma_in_idx = 0;

    /* Determine which CS line to assert. Default to CS0. */
    int cs_line = 0;
    uint32_t cs_dis_mask = 1 << cs_line;
    bool cs_disabled = (s->misc & cs_dis_mask) != 0;

    /* Get GDMA channels configured for SPI2 */
    bool have_out = esp_gdma_get_channel_periph(s->gdma, GDMA_SPI2, ESP_GDMA_OUT_IDX, &gdma_out_idx);
    bool have_in = esp_gdma_get_channel_periph(s->gdma, GDMA_SPI2, ESP_GDMA_IN_IDX, &gdma_in_idx);

    /* Get data length from MS_DLEN register */
    uint32_t data_bitlen = FIELD_EX32(s->ms_dlen, GPSPI2_MS_DLEN, MS_DATA_BITLEN);
    uint32_t data_len = (data_bitlen + 1) / 8;

#if SPI2_DEBUG
    info_report("[SPI2] DMA TRANSACTION: data_len=%u have_out=%d have_in=%d",
                data_len, have_out, have_in);
#endif

    /* Assert CS low */
    if (s->cs_state[cs_line] != 0) {
#if SPI2_DEBUG
        info_report("[SPI2] DMA: CS%d -> LOW", cs_line);
#endif
        qemu_set_irq(s->cs_gpio[cs_line], 0);
        s->cs_state[cs_line] = 0;
    }

    /* Command phase */
    if (s->user & R_GPSPI2_USER_USR_COMMAND_MASK) {
        uint32_t cmd_bitlen = FIELD_EX32(s->user2, GPSPI2_USER2, USR_COMMAND_BITLEN);
        uint32_t cmd_bytes = (cmd_bitlen + 1) / 8;
        uint32_t cmd_value = FIELD_EX32(s->user2, GPSPI2_USER2, USR_COMMAND_VALUE);

#if SPI2_DEBUG
        info_report("[SPI2] DMA TX CMD: val=0x%x bytes=%u", cmd_value, cmd_bytes);
#endif
        for (int i = cmd_bytes - 1; i >= 0; i--) {
            ssi_transfer(s->spi, (cmd_value >> (i * 8)) & 0xFF);
        }
    }

    /* Address phase */
    if (s->user & R_GPSPI2_USER_USR_ADDR_MASK) {
        uint32_t addr_bitlen = FIELD_EX32(s->user1, GPSPI2_USER1, USR_ADDR_BITLEN);
        uint32_t addr_bytes = (addr_bitlen + 1) / 8;
        uint32_t addr_be = bswap32(s->addr);

        if (addr_bytes > 0 && addr_bytes <= 4) {
            addr_be = addr_be >> (32 - addr_bytes * 8);
        }

#if SPI2_DEBUG
        info_report("[SPI2] DMA TX ADDR: 0x%x bytes=%u", s->addr, addr_bytes);
#endif
        uint8_t *addr_ptr = (uint8_t *)&addr_be;
        for (uint32_t i = 0; i < addr_bytes; i++) {
            ssi_transfer(s->spi, addr_ptr[i]);
        }
    }

    /* Dummy phase */
    if (s->user & R_GPSPI2_USER_USR_DUMMY_MASK) {
        uint32_t dummy_cyclelen = FIELD_EX32(s->user1, GPSPI2_USER1, USR_DUMMY_CYCLELEN);
        uint32_t dummy_bytes = (dummy_cyclelen + 1 + 7) / 8;
#if SPI2_DEBUG
        info_report("[SPI2] DMA DUMMY: %u bytes", dummy_bytes);
#endif
        for (uint32_t i = 0; i < dummy_bytes; i++) {
            ssi_transfer(s->spi, 0x00);
        }
    }

    /* Data phase via DMA */
    if (data_len > 0) {
        bool do_mosi = (s->user & R_GPSPI2_USER_USR_MOSI_MASK) != 0;
        bool do_miso = (s->user & R_GPSPI2_USER_USR_MISO_MASK) != 0;

        /* Allocate buffer for transfer */
        uint8_t *buffer = g_malloc(data_len);
        if (buffer == NULL) {
            error_report("[SPI2] Failed to allocate DMA buffer of %u bytes", data_len);
            goto complete;
        }

        /* Read TX data from GDMA OUT channel */
        if (do_mosi && have_out) {
            if (!esp_gdma_read_channel(s->gdma, gdma_out_idx, buffer, data_len)) {
                warn_report("[SPI2] Error reading from GDMA OUT channel");
                memset(buffer, 0xFF, data_len);
            }
#if SPI2_DEBUG
            info_report("[SPI2] DMA TX: read %u bytes from GDMA, first: %02x %02x %02x %02x",
                        data_len,
                        data_len > 0 ? buffer[0] : 0, data_len > 1 ? buffer[1] : 0,
                        data_len > 2 ? buffer[2] : 0, data_len > 3 ? buffer[3] : 0);
#endif
        } else {
            /* Fill with 0xFF for RX-only transfers */
            memset(buffer, 0xFF, data_len);
        }

        /* Perform SPI transfer */
        for (uint32_t i = 0; i < data_len; i++) {
            uint32_t rx_byte = ssi_transfer(s->spi, buffer[i]);
            buffer[i] = (uint8_t)rx_byte;
        }

        /* Write RX data to GDMA IN channel */
        if (do_miso && have_in) {
#if SPI2_DEBUG
            info_report("[SPI2] DMA RX: writing %u bytes to GDMA, first: %02x %02x %02x %02x",
                        data_len,
                        data_len > 0 ? buffer[0] : 0, data_len > 1 ? buffer[1] : 0,
                        data_len > 2 ? buffer[2] : 0, data_len > 3 ? buffer[3] : 0);
#endif
            if (!esp_gdma_write_channel(s->gdma, gdma_in_idx, buffer, data_len)) {
                warn_report("[SPI2] Error writing to GDMA IN channel");
            }
        }

        g_free(buffer);
    }

    /* Deassert CS high if appropriate */
    if (!(s->misc & R_GPSPI2_MISC_CS_KEEP_ACTIVE_MASK) && !cs_disabled) {
        if (s->cs_state[cs_line] != 1) {
#if SPI2_DEBUG
            info_report("[SPI2] DMA: CS%d -> HIGH", cs_line);
#endif
            qemu_set_irq(s->cs_gpio[cs_line], 1);
            s->cs_state[cs_line] = 1;
        }
    }

complete:
    /* Clear USR bit and set TRANS_DONE */
    s->cmd &= ~R_GPSPI2_CMD_USR_MASK;
    s->dma_int_raw |= R_GPSPI2_DMA_INT_RAW_TRANS_DONE_MASK;
    esp32c3_spi2_update_irq(s);
}


static void esp32c3_spi2_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned int size)
{
    ESP32C3Spi2State *s = ESP32C3_SPI2(opaque);
    uint32_t wvalue = (uint32_t)value;

    /* Debug all significant writes */
#if SPI2_DEBUG
    if (addr != A_GPSPI2_DMA_INT_ST) {  /* Skip read-only register */
        info_report("[SPI2] WRITE 0x%02lx = 0x%08x", addr, wvalue);
    }
#endif

    switch (addr) {
        case A_GPSPI2_CMD:
            s->cmd = wvalue;
            if (wvalue & R_GPSPI2_CMD_UPDATE_MASK) {
                /* SPI_UPDATE bit: accept and clear (no-op for emulation;
                 * real HW uses this to sync APB -> SPI clock domain) */
                s->cmd &= ~R_GPSPI2_CMD_UPDATE_MASK;
            }
            if (wvalue & R_GPSPI2_CMD_USR_MASK) {
#if SPI2_DEBUG
                info_report("[SPI2] TRIGGER: user=0x%08x user1=0x%08x user2=0x%08x "
                            "ms_dlen=0x%x addr=0x%x dma_conf=0x%x "
                            "W0=0x%08x W1=0x%08x W2=0x%08x W3=0x%08x",
                            s->user, s->user1, s->user2,
                            s->ms_dlen, s->addr, s->dma_conf,
                            s->data_reg[0], s->data_reg[1],
                            s->data_reg[2], s->data_reg[3]);
#endif
                /* Check if DMA mode should be used */
                if (esp32c3_spi2_use_dma_mode(s)) {
                    esp32c3_spi2_dma_transaction(s);
                } else {
                    esp32c3_spi2_begin_transaction(s);
                }
            }
            break;
        case A_GPSPI2_ADDR:
            s->addr = wvalue;
            break;
        case A_GPSPI2_CTRL:
            s->ctrl = wvalue;
            break;
        case A_GPSPI2_CLOCK:
            s->clock = wvalue;
            break;
        case A_GPSPI2_USER:
            s->user = wvalue;
            break;
        case A_GPSPI2_USER1:
            s->user1 = wvalue;
            break;
        case A_GPSPI2_USER2:
            s->user2 = wvalue;
            break;
        case A_GPSPI2_MS_DLEN:
            s->ms_dlen = wvalue;
            break;
        case A_GPSPI2_MISC:
            s->misc = wvalue;
            break;
        case A_GPSPI2_DMA_CONF:
            /* Accept DMA configuration writes; handle AFIFO reset bits as write-1-to-clear.
             * We don't actually implement DMA, but the SPI master driver configures this. */
            s->dma_conf = wvalue & ~(R_GPSPI2_DMA_CONF_RX_AFIFO_RST_MASK |
                                     R_GPSPI2_DMA_CONF_BUF_AFIFO_RST_MASK |
                                     R_GPSPI2_DMA_CONF_DMA_AFIFO_RST_MASK);
            break;
        case A_GPSPI2_DMA_INT_ENA:
            s->dma_int_ena = wvalue;
            esp32c3_spi2_update_irq(s);
            break;
        case A_GPSPI2_DMA_INT_CLR:
            /* Writing 1 to a bit clears the corresponding bit in DMA_INT_RAW */
            s->dma_int_raw &= ~wvalue;
            esp32c3_spi2_update_irq(s);
            break;
        case A_GPSPI2_DMA_INT_RAW:
            /* RAW register is typically not written by software, but accept writes */
            s->dma_int_raw = wvalue;
            esp32c3_spi2_update_irq(s);
            break;
        case A_GPSPI2_W0 ... A_GPSPI2_W15:
            s->data_reg[(addr - A_GPSPI2_W0) / sizeof(uint32_t)] = wvalue;
            break;
        /* QEMU extension: additional W registers for large CPU-mode transfers */
        case GPSPI2_W_EXT_BASE ... (GPSPI2_W_EXT_END - 1): {
            uint32_t idx = 16 + (addr - GPSPI2_W_EXT_BASE) / sizeof(uint32_t);
            if (idx < ESP32C3_SPI2_BUF_WORDS) {
                s->data_reg[idx] = wvalue;
            }
            break;
        }
        case A_GPSPI2_SLAVE:
            s->slave = wvalue;
            break;
        case A_GPSPI2_CLK_GATE:
            s->clk_gate = wvalue;
            break;
        case A_GPSPI2_DATE:
            s->date = wvalue;
            break;
        case A_GPSPI2_DMA_INT_ST:
            /* DMA_INT_ST is read-only (computed from RAW & ENA) - ignore writes */
            break;
        case 0x24: /* SPI_DIN_MODE_REG */
        case 0x28: /* SPI_DIN_NUM_REG */
        case 0x2C: /* SPI_DOUT_MODE_REG */
        case 0x44: /* Additional registers */
        case 0x48:
        case 0x4C:
        case 0x50:
        case 0x54:
        case 0x58:
        case 0x5C:
        case 0x60:
        case 0x64:
        case 0x68:
        case 0x6C:
        case 0x70:
        case 0x74:
        case 0x78:
        case 0x7C:
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8C:
        case 0x90:
        case 0x94:
        case 0xD8:
        case 0xDC:
        case 0xE4: /* SPI_SLAVE1_REG */
            /* Accept but ignore timing/mode/other registers */
            break;
        default:
#if SPI2_DEBUG
            warn_report("[SPI2] Unsupported write to 0x%lx (%08lx)", addr, value);
#endif
            break;
    }
}


static const MemoryRegionOps esp32c3_spi2_ops = {
    .read = esp32c3_spi2_read,
    .write = esp32c3_spi2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp32c3_spi2_reset_hold(Object *obj, ResetType type)
{
    ESP32C3Spi2State *s = ESP32C3_SPI2(obj);

    s->cmd = 0;
    s->addr = 0;
    s->ctrl = 0;
    s->clock = 0;
    s->user = 0;
    s->user1 = 0;
    s->user2 = 0;
    s->ms_dlen = 0;
    s->misc = 0;
    s->dma_conf = 0;
    s->dma_int_ena = 0;
    s->dma_int_clr = 0;
    s->dma_int_raw = 0;
    s->dma_int_st = 0;
    s->slave = 0;
    s->clk_gate = 0;
    s->date = 0;
    memset(s->data_reg, 0, ESP32C3_SPI2_BUF_WORDS * sizeof(uint32_t));
    /* CS starts high (deasserted) */
    for (int i = 0; i < ESP32C3_SPI2_CS_COUNT; i++) {
        s->cs_state[i] = 1;
    }
}


static void esp32c3_spi2_realize(DeviceState *dev, Error **errp)
{
    ESP32C3Spi2State *s = ESP32C3_SPI2(dev);

    /* GDMA is optional - if not set, only CPU mode will work */
    if (s->gdma == NULL) {
        info_report("[SPI2] GDMA not configured, DMA mode disabled");
    }
}


static void esp32c3_spi2_init(Object *obj)
{
    ESP32C3Spi2State *s = ESP32C3_SPI2(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_spi2_ops, s,
                          TYPE_ESP32C3_SPI2, ESP32C3_SPI2_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    esp32c3_spi2_reset_hold(obj, RESET_TYPE_COLD);

    s->spi = ssi_create_bus(DEVICE(s), "spi");
    qdev_init_gpio_out_named(DEVICE(s), s->cs_gpio, SSI_GPIO_CS,
                             ESP32C3_SPI2_CS_COUNT);
}


static Property esp32c3_spi2_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};


static void esp32c3_spi2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c3_spi2_reset_hold;
    dc->realize = esp32c3_spi2_realize;
    device_class_set_props(dc, esp32c3_spi2_properties);
}


static const TypeInfo esp32c3_spi2_info = {
    .name = TYPE_ESP32C3_SPI2,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3Spi2State),
    .instance_init = esp32c3_spi2_init,
    .class_init = esp32c3_spi2_class_init,
};


static void esp32c3_spi2_register_types(void)
{
    type_register_static(&esp32c3_spi2_info);
}

type_init(esp32c3_spi2_register_types)

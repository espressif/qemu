/*
 * ESP32-C3 I2S controller emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * This model emulates the ESP32-C3 I2S0 peripheral well enough for the ESP-IDF
 * I2S driver (esp_driver_i2s, standard mode, TX) to run under QEMU without
 * stalling. It does NOT emulate the audio serial protocol; instead it drains the
 * GDMA OUT channel bound to I2S0 (raising OUT_EOF so the driver's DMA callback
 * unblocks i2s_channel_write()) and optionally forwards the raw PCM to a chardev.
 */

#pragma once

#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "hw/dma/esp_gdma.h"

#define TYPE_ESP32C3_I2S "esp32c3.i2s"
#define ESP32C3_I2S(obj) OBJECT_CHECK(Esp32C3I2SState, (obj), TYPE_ESP32C3_I2S)

/* Size of the MMIO region: the peripheral occupies a 4 KB page. */
#define ESP32C3_I2S_MEM_SIZE    0x1000
#define ESP32C3_I2S_REG_CNT     (ESP32C3_I2S_MEM_SIZE / sizeof(uint32_t))

/* DATE/version register value (mirrors real silicon closely enough for the driver). */
#define ESP32C3_I2S_DATE_VALUE  0x1908140

typedef struct Esp32C3I2SState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* Device interrupt line, wired to the interrupt matrix. Optional for the
     * no-stall path (the GDMA OUT_EOF IRQ is what actually unblocks the driver). */
    qemu_irq irq;

    /* Public: must be set before realize. GDMA controller used to fetch audio buffers. */
    ESPGdmaState *gdma;

    /* Optional PCM output sink (raw interleaved 16-bit LE samples). */
    CharBackend chr;

    /* Periodic timer that drains one DMA buffer per tick while TX is running. */
    QEMUTimer tx_timer;
    bool tx_running;

    /* Flat register backing store, indexed by (offset / 4). */
    uint32_t regs[ESP32C3_I2S_REG_CNT];
} Esp32C3I2SState;


/*
 * Register offsets and fields, from the ESP32-C3 TRM / i2s_reg.h.
 * Only the registers the driver actually touches on the TX path are interpreted;
 * everything else is stored/returned verbatim via the backing store.
 */

REG32(I2S_INT_RAW, 0x00C)
    FIELD(I2S_INT_RAW, RX_DONE, 0, 1)
    FIELD(I2S_INT_RAW, TX_DONE, 1, 1)
    FIELD(I2S_INT_RAW, RX_HUNG, 2, 1)
    FIELD(I2S_INT_RAW, TX_HUNG, 3, 1)

REG32(I2S_INT_ST, 0x010)
REG32(I2S_INT_ENA, 0x014)
REG32(I2S_INT_CLR, 0x018)

REG32(I2S_RX_CONF, 0x020)
    FIELD(I2S_RX_CONF, RX_RESET,      0, 1)
    FIELD(I2S_RX_CONF, RX_FIFO_RESET, 1, 1)
    FIELD(I2S_RX_CONF, RX_START,      2, 1)
    FIELD(I2S_RX_CONF, RX_UPDATE,     8, 1)

REG32(I2S_TX_CONF, 0x024)
    FIELD(I2S_TX_CONF, TX_RESET,      0, 1)
    FIELD(I2S_TX_CONF, TX_FIFO_RESET, 1, 1)
    FIELD(I2S_TX_CONF, TX_START,      2, 1)
    FIELD(I2S_TX_CONF, TX_MONO,       5, 1)
    FIELD(I2S_TX_CONF, TX_UPDATE,     8, 1)

REG32(I2S_RX_CONF1, 0x028)

REG32(I2S_TX_CONF1, 0x02C)
    FIELD(I2S_TX_CONF1, TX_BCK_DIV_NUM, 7, 6)
    FIELD(I2S_TX_CONF1, TX_BITS_MOD,   13, 5)

REG32(I2S_RX_CLKM_CONF, 0x030)

REG32(I2S_TX_CLKM_CONF, 0x034)
    FIELD(I2S_TX_CLKM_CONF, TX_CLKM_DIV_NUM, 0, 8)
    FIELD(I2S_TX_CLKM_CONF, TX_CLK_ACTIVE,  26, 1)
    FIELD(I2S_TX_CLKM_CONF, TX_CLK_SEL,     27, 2)
    FIELD(I2S_TX_CLKM_CONF, TX_CLK_EN,      29, 1)

REG32(I2S_RX_CLKM_DIV_CONF, 0x038)

REG32(I2S_TX_CLKM_DIV_CONF, 0x03C)
    FIELD(I2S_TX_CLKM_DIV_CONF, TX_CLKM_DIV_Z,   0, 9)
    FIELD(I2S_TX_CLKM_DIV_CONF, TX_CLKM_DIV_Y,   9, 9)
    FIELD(I2S_TX_CLKM_DIV_CONF, TX_CLKM_DIV_X,  18, 9)
    FIELD(I2S_TX_CLKM_DIV_CONF, TX_CLKM_DIV_YN1, 27, 1)

REG32(I2S_RX_TIMING, 0x058)
REG32(I2S_TX_TIMING, 0x05C)

REG32(I2S_DATE, 0x080)

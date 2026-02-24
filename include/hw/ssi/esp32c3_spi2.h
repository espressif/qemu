/*
 * ESP32-C3 GPSPI2 (general-purpose SPI2) controller
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32C3_SPI2_H
#define ESP32C3_SPI2_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "hw/registerfields.h"
#include "hw/dma/esp_gdma.h"

#define TYPE_ESP32C3_SPI2 "esp32c3-spi2"
#define ESP32C3_SPI2(obj) OBJECT_CHECK(ESP32C3Spi2State, (obj), TYPE_ESP32C3_SPI2)

/* Number of chip select lines */
#define ESP32C3_SPI2_CS_COUNT 3

/* Number of 32-bit data registers (W0-W15)
 * Real hardware is 16 words (64 bytes), but for QEMU we increase this to
 * support larger transfers without DMA (needed for SD card sector reads) */
#define ESP32C3_SPI2_BUF_WORDS 256  /* 1024 bytes for QEMU testing */

/* IO region size */
#define ESP32C3_SPI2_IO_SIZE 0x1000

/* Register field definitions using REG32 which creates A_* enum values */
REG32(GPSPI2_CMD, 0x00)
    FIELD(GPSPI2_CMD, USR, 24, 1)
    FIELD(GPSPI2_CMD, UPDATE, 23, 1)

REG32(GPSPI2_ADDR, 0x04)

REG32(GPSPI2_CTRL, 0x08)

REG32(GPSPI2_CLOCK, 0x0C)

REG32(GPSPI2_USER, 0x10)
    FIELD(GPSPI2_USER, USR_COMMAND, 31, 1)
    FIELD(GPSPI2_USER, USR_ADDR, 30, 1)
    FIELD(GPSPI2_USER, USR_DUMMY, 29, 1)
    FIELD(GPSPI2_USER, USR_MISO, 28, 1)
    FIELD(GPSPI2_USER, USR_MOSI, 27, 1)
    FIELD(GPSPI2_USER, DOUTDIN, 0, 1)

REG32(GPSPI2_USER1, 0x14)
    FIELD(GPSPI2_USER1, USR_ADDR_BITLEN, 26, 5)
    FIELD(GPSPI2_USER1, USR_DUMMY_CYCLELEN, 0, 8)

REG32(GPSPI2_USER2, 0x18)
    FIELD(GPSPI2_USER2, USR_COMMAND_BITLEN, 28, 4)
    FIELD(GPSPI2_USER2, USR_COMMAND_VALUE, 0, 16)

REG32(GPSPI2_MS_DLEN, 0x1C)
    FIELD(GPSPI2_MS_DLEN, MS_DATA_BITLEN, 0, 18)

REG32(GPSPI2_MISC, 0x20)
    FIELD(GPSPI2_MISC, CS_KEEP_ACTIVE, 10, 1)

/* Register addresses from ESP32-C3 Technical Reference Manual */
REG32(GPSPI2_DMA_CONF, 0x30)
    FIELD(GPSPI2_DMA_CONF, RX_AFIFO_RST, 30, 1)
    FIELD(GPSPI2_DMA_CONF, BUF_AFIFO_RST, 29, 1)
    FIELD(GPSPI2_DMA_CONF, DMA_AFIFO_RST, 28, 1)

REG32(GPSPI2_DMA_INT_ENA, 0x34)
    FIELD(GPSPI2_DMA_INT_ENA, TRANS_DONE, 12, 1)

REG32(GPSPI2_DMA_INT_CLR, 0x38)
    FIELD(GPSPI2_DMA_INT_CLR, TRANS_DONE, 12, 1)

REG32(GPSPI2_DMA_INT_RAW, 0x3C)
    FIELD(GPSPI2_DMA_INT_RAW, TRANS_DONE, 12, 1)

REG32(GPSPI2_DMA_INT_ST, 0x40)
    FIELD(GPSPI2_DMA_INT_ST, TRANS_DONE, 12, 1)

/* Data registers W0-W15 at 0x98-0xD4 (real HW)
 * For QEMU testing of large CPU-mode transfers, we extend the buffer
 * and add additional W registers at 0x100-0x4FC (W16-W255).
 * This is not real hardware - it's a QEMU extension for SD card testing. */
REG32(GPSPI2_W0, 0x98)

REG32(GPSPI2_W15, 0xD4)

/* QEMU extension: additional W registers for large transfers */
#define GPSPI2_W_EXT_BASE 0x100
#define GPSPI2_W_EXT_END  (GPSPI2_W_EXT_BASE + (ESP32C3_SPI2_BUF_WORDS - 16) * 4)

REG32(GPSPI2_SLAVE, 0xE0)

REG32(GPSPI2_CLK_GATE, 0xE8)

REG32(GPSPI2_DATE, 0xF0)

typedef struct ESP32C3Spi2State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    SSIBus *spi;
    qemu_irq cs_gpio[ESP32C3_SPI2_CS_COUNT];

    /* Registers */
    uint32_t cmd;
    uint32_t addr;
    uint32_t ctrl;
    uint32_t clock;
    uint32_t user;
    uint32_t user1;
    uint32_t user2;
    uint32_t ms_dlen;
    uint32_t misc;
    uint32_t dma_conf;
    uint32_t dma_int_ena;
    uint32_t dma_int_clr;
    uint32_t dma_int_raw;
    uint32_t dma_int_st;
    uint32_t slave;
    uint32_t clk_gate;
    uint32_t date;
    uint32_t data_reg[ESP32C3_SPI2_BUF_WORDS];
    /* Track CS line state to avoid spurious edges */
    int cs_state[ESP32C3_SPI2_CS_COUNT];

    /* GDMA controller for DMA transfers - must be set by machine before realize */
    ESPGdmaState *gdma;
} ESP32C3Spi2State;

#endif /* ESP32C3_SPI2_H */

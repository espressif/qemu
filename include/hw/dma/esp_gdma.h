/*
 * GDMA emulation for recent ESP32-series chip (ESP32-S3 and newer)
 *
 * Copyright (c) 2023-2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"

#define TYPE_ESP_GDMA "esp.gdma"
#define ESP_GDMA(obj)               OBJECT_CHECK(ESPGdmaState, (obj), TYPE_ESP_GDMA)
#define ESP_GDMA_GET_CLASS(obj)     OBJECT_GET_CLASS(ESPGdmaClass, obj, TYPE_ESP_GDMA)
#define ESP_GDMA_CLASS(klass)       OBJECT_CLASS_CHECK(ESPGdmaClass, klass, TYPE_ESP_GDMA)


#define ESP_GDMA_IN_IDX     0
#define ESP_GDMA_OUT_IDX    1
#define ESP_GDMA_CONF_COUNT (ESP_GDMA_OUT_IDX + 1)


#define ESP_GDMA_RAM_ADDR   0x3FC80000

/**
 * @brief Names for the IN and OUT IRQs pins, can be passed to `qdev_connect_gpio_out_named`.
 */
#define ESP_GDMA_IRQ_IN_NAME    "CHAN_IN"
#define ESP_GDMA_IRQ_OUT_NAME   "CHAN_OUT"


/**
 * @brief Number for each peripheral that can access GDMA
 */
typedef enum {
    GDMA_SPI2   = 0,
    GDMA_SPI3   = 1,
    GDMA_UHCI0  = 2,
    GDMA_I2S0   = 3,
    GDMA_I2S1   = 4,
    GDMA_LCDCAM = 5,
    GDMA_AES    = 6,
    GDMA_SHA    = 7,
    GDMA_ADC    = 8,
    GDMA_RMT    = 9,
    GDMA_LAST   = GDMA_RMT,
} GdmaPeripheral;


/**
 * @brief Size of the interrupt registers, in bytes, for a single channel
 */
#define ESP_GDMA_INT_REGS_SIZE  0x10

typedef struct {
    uint32_t raw;
    uint32_t st;
    uint32_t ena;
    qemu_irq irq;
} DmaIntState;


/* Define the generic/virtual registers the inherited class can pass to this class */
typedef enum {
    GDMA_UNKNOWN = -1,
    GDMA_CONF0_REG = 0,
    GDMA_CONF1_REG,
    GDMA_INT_RAW_REG,
    GDMA_INT_ST_REG,
    GDMA_INT_ENA_REG,
    GDMA_INT_CLR_REG,
    GDMA_FIFO_ST_REG,
    GDMA_POP_REG,
    GDMA_LINK_REG,
    GDMA_STATE_REG,
    GDMA_SUC_EOF_DESC_REG,
    GDMA_ERR_EOF_DESC_REG,
    GDMA_DESC_ADDR_REG,
    GDMA_BF0_DESC_ADDR_REG,
    GDMA_BF1_DESC_ADDR_REG,
    GDMA_DUMMY_3C_REG,
    GDMA_DUMMY_40_REG,
    GDMA_PRIORITY_REG,
    GDMA_PERI_SEL_REG,
    GDMA_MISC_REG,
} DmaRegister;


typedef struct {
    /* Configuration registers */
    uint32_t conf0;
    uint32_t conf1;
    uint32_t status;
    uint32_t push_pop;
    uint32_t link;
    /* Status registers */
    uint32_t state;
    uint32_t suc_eof_desc_addr; // Address of descriptor when EOF bit is 1
    uint32_t err_eof_desc_addr; // Address of descriptor when error occurs (UHCI0 only)
    uint32_t desc_addr;         // Address of the next descriptor (n + 1)
    uint32_t bfr_desc_addr;     // Address of the current descriptor (n)
    uint32_t bfr_bfr_desc_addr; // Address of the previous descriptor (n - 1)
    uint32_t priority;
    uint32_t peripheral;
    /* Interrupt related registers */
    DmaIntState int_state;
} DmaConfigState;


typedef struct ESPGdmaState {
    SysBusDevice parent_object;

    DmaConfigState* ch_conf[ESP_GDMA_CONF_COUNT];
    /* Use this register mainly for enabling and disabling priorities */
    uint32_t misc_conf;
    /* Keep a pointer to the SoC DRAM */
    MemoryRegion* soc_mr;
    AddressSpace dma_as;
} ESPGdmaState;


typedef struct ESPGdmaClass {
    SysBusDeviceClass parent_class;

    /* All the attributes and method that are common to all instances must be stored here */
    size_t m_channel_count;

    /* Virtual methods */
    /**
     * @brief Checks if the given peripheral, from GdmaPeripheral enumeration, is invalid/reserved.
     *        If NULL, all the peripherals are considered valid.
     */
    bool (*is_periph_invalid)(ESPGdmaState *s, GdmaPeripheral per);
} ESPGdmaClass;


/**
 * @brief Get the channel configured for the given peripheral
 *
 * @param s GDMA state
 * @param periph Peripheral to search
 * @param dir Direction from the GDMA point of view: ESP_GDMA_IN_IDX or ESP_GDMA_OUT_IDX.
 *            For example, to find a channel that needs to be written to, use ESP_GDMA_IN_IDX
 *            (because GDMA receives the data)
 * @param chan Returned channel index linked to the peripheral
 *
 * @returns true  if the peripheral was found and the index of the GDMA channel it is bound to is stored in `chan`,
 *          false if the peripheral was not found or invalid.
 */
bool esp_gdma_get_channel_periph(ESPGdmaState *s, GdmaPeripheral periph, int dir,
                                     uint32_t* chan);

bool esp_gdma_read_channel(ESPGdmaState *s, uint32_t chan, uint8_t* buffer, uint32_t size);
bool esp_gdma_write_channel(ESPGdmaState *s, uint32_t chan, uint8_t* buffer, uint32_t size);

/**
 * @brief Peek at the number of valid bytes (`length`) advertised by the head OUT descriptor
 *        of the given channel, without consuming/advancing it.
 *
 * Useful for a continuous peripheral (e.g. I2S TX) that wants to drain exactly one DMA
 * buffer per step: read the length here, then call esp_gdma_read_channel() with it so that
 * exactly one descriptor (and thus one OUT_EOF) is processed.
 *
 * @param s    GDMA state
 * @param chan OUT channel index
 * @param len  Filled with the head descriptor's `length` field on success
 *
 * @returns true if a descriptor could be read, false otherwise
 */
bool esp_gdma_peek_length(ESPGdmaState *s, uint32_t chan, uint32_t *len);


/**
 * @brief Function only meant to be used by inherited classes
 */
void esp_gdma_write_chan_register(ESPGdmaState* s, uint32_t dir, uint32_t chan, DmaRegister reg, uint32_t value);
void esp_gdma_write_register(ESPGdmaState* s, DmaRegister reg, uint32_t value);

uint64_t esp_gdma_read_chan_register(ESPGdmaState* state, uint32_t dir, uint32_t chan, DmaRegister reg);
uint64_t esp_gdma_read_register(ESPGdmaState* s, DmaRegister reg);


/**
 * @brief Define virtual registers and their generic fields for the I/Os.
 * The addresses for the registers are arbitrary, they don't respect any real hardware
 * address, however, the fields correspond to the real ESP targets ones.
 * We can define here since they are (mostly) the same for all the supported atrgets (C3 and S3).
 * When porting this GDMA component to a new target, make SURE that these bitfields are valid!
 * If any of these bits is undefined/reserved on the target, make sure to mask it before
 * passing it to this generic GDMA component.
 */
REG32(GDMA_IN_CONF0, 0x000)
    FIELD(GDMA_IN_CONF0, MEM_TRANS_EN,  4, 1)
    FIELD(GDMA_IN_CONF0, DATA_BURST_EN, 3, 1)
    FIELD(GDMA_IN_CONF0, DSCR_BURST_EN, 2, 1)
    FIELD(GDMA_IN_CONF0, LOOP_TEST,     1, 1)
    FIELD(GDMA_IN_CONF0, RST,           0, 1)


REG32(GDMA_IN_CONF1, 0x000)
    FIELD(GDMA_IN_CONF1, EXT_MEM_BK_SIZE, 13, 2) // Reserved on C3
    FIELD(GDMA_IN_CONF1, CHECK_OWNER,     12, 1)
    FIELD(GDMA_IN_CONF1, FIFO_FULL_THRS,  0, 12) // Reserved on C3


REG32(GDMA_OUT_CONF0, 0x000)
    FIELD(GDMA_OUT_CONF0, DATA_BURST_EN, 5, 1)
    FIELD(GDMA_OUT_CONF0, DSCR_BURST_EN, 4, 1)
    FIELD(GDMA_OUT_CONF0, EOF_MODE,      3, 1)
    FIELD(GDMA_OUT_CONF0, AUTO_WRBACK,   2, 1)
    FIELD(GDMA_OUT_CONF0, LOOP_TEST,     1, 1)
    FIELD(GDMA_OUT_CONF0, RST,           0, 1)


REG32(GDMA_OUT_CONF1, 0x000)
    FIELD(GDMA_OUT_CONF1, EXT_MEM_BK_SIZE, 13, 2) // Reserved on C3
    FIELD(GDMA_OUT_CONF1, CHECK_OWNER,     12, 1)


REG32(GDMA_IN_LINK, 0x000)
    FIELD(GDMA_IN_LINK, PARK,     24, 1)
    FIELD(GDMA_IN_LINK, RESTART,  23, 1)
    FIELD(GDMA_IN_LINK, START,    22, 1)
    FIELD(GDMA_IN_LINK, STOP,     21, 1)
    FIELD(GDMA_IN_LINK, AUTO_RET, 20, 1)
    FIELD(GDMA_IN_LINK, ADDR,     0, 20)


REG32(GDMA_OUT_LINK, 0x000)
    FIELD(GDMA_OUT_LINK, PARK,    23, 1)
    FIELD(GDMA_OUT_LINK, RESTART, 22, 1)
    FIELD(GDMA_OUT_LINK, START,   21, 1)
    FIELD(GDMA_OUT_LINK, STOP,    20, 1)
    FIELD(GDMA_OUT_LINK, ADDR,    0, 20)


REG32(GDMA_INFIFO_STATUS, 0x000)
    FIELD(GDMA_INFIFO_STATUS, FIFO_EMPTY, 1, 1)
    

REG32(GDMA_OUT_STATE, 0x000)
    FIELD(GDMA_OUT_STATE, STATE,          20, 3)
    FIELD(GDMA_OUT_STATE, DSCR_STATE,     18, 2)
    FIELD(GDMA_OUT_STATE, LINK_DSCR_ADDR, 0, 18)


/* IN/OUT PERI registers have the same organization, define a common register */
REG32(GDMA_PERI_SEL, 0x000)
    FIELD(GDMA_PERI_SEL, PERI_SEL, 0, 6)


REG32(GDMA_MISC_CONF, 0x000)


REG32(GDMA_INTERRUPT, 0x000)
    FIELD(GDMA_INTERRUPT, IN_DSCR_EMPTY, 4, 1)
    FIELD(GDMA_INTERRUPT, IN_DSCR_ERR,   3, 1)
    FIELD(GDMA_INTERRUPT, IN_ERR_EOF,    2, 1)
    FIELD(GDMA_INTERRUPT, IN_SUC_EOF,    1, 1)
    FIELD(GDMA_INTERRUPT, IN_DONE,       0, 1)

    FIELD(GDMA_INTERRUPT, OUT_TOTAL_EOF,  3, 1)
    FIELD(GDMA_INTERRUPT, OUT_DSCR_ERR,   2, 1)
    FIELD(GDMA_INTERRUPT, OUT_EOF,        1, 1)
    FIELD(GDMA_INTERRUPT, OUT_DONE,       0, 1)

/*
 * ESP32-C3 GDMA emulation
 *
 * Copyright (c) 2023-2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/dma/esp32c3_gdma.h"


#define DIM(arr) (sizeof(arr)/sizeof(*arr))
#define ESP32C3_GDMA_WARNING    0
#define ESP32C3_GDMA_DEBUG      0


typedef struct {
    uint32_t specific;
    uint32_t generic;
} ESP32C3IntMapping;

/**
 * @brief Constant mapping used to easily convert an C3 specific interrupt bitmap into a generic
 * interrupt register bitmap.
 */

const ESP32C3IntMapping int_mapping[] = {
    { .specific = R_DMA_INT_RAW_IN_DSCR_EMPTY_MASK, .generic = R_GDMA_INTERRUPT_IN_DSCR_EMPTY_MASK },
    { .specific = R_DMA_INT_RAW_IN_DSCR_ERR_MASK,   .generic = R_GDMA_INTERRUPT_IN_DSCR_ERR_MASK   },
    { .specific = R_DMA_INT_RAW_IN_ERR_EOF_MASK,    .generic = R_GDMA_INTERRUPT_IN_ERR_EOF_MASK    },
    { .specific = R_DMA_INT_RAW_IN_SUC_EOF_MASK,    .generic = R_GDMA_INTERRUPT_IN_SUC_EOF_MASK    },
    { .specific = R_DMA_INT_RAW_IN_DONE_MASK,       .generic = R_GDMA_INTERRUPT_IN_DONE_MASK       },
    { .specific = R_DMA_INT_RAW_OUT_TOTAL_EOF_MASK, .generic = R_GDMA_INTERRUPT_OUT_TOTAL_EOF_MASK },
    { .specific = R_DMA_INT_RAW_OUT_DSCR_ERR_MASK,  .generic = R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK  },
    { .specific = R_DMA_INT_RAW_OUT_EOF_MASK,       .generic = R_GDMA_INTERRUPT_OUT_EOF_MASK       },
    { .specific = R_DMA_INT_RAW_OUT_DONE_MASK,      .generic = R_GDMA_INTERRUPT_OUT_DONE_MASK      },
};


/**
 * @brief Extract and generate a generic interrupt bitmap for IN and OUT channels out of an ESP32-C3 interrupt bitmap.
 * This is necessary since the ESP32-C3 interrupt registers gather both the IN and OUT interrupt status
 * bits in the same register while the generic implementation seperate them into two different registers.
 */
static void esp32c3_to_int_bitmask(uint32_t value, uint32_t *in_value, uint32_t *out_value)
{
    assert(in_value != NULL && out_value != NULL);
    int i = 0;

    /* IN bitmasks are first in the mapping */
    for (i = 0; i < DIM(int_mapping) / 2; i++) {
        if (value & int_mapping[i].specific) {
            *in_value |= int_mapping[i].generic;
        }
    }
    /* OUT bitmasks are following */
    for (; i < DIM(int_mapping); i++) {
        if (value & int_mapping[i].specific) {
            *out_value |= int_mapping[i].generic;
        }
    }
}


/**
 * @brief Extract and generate a generic interrupt bitmap for IN and OUT channels out of an ESP32-C3 interrupt bitmap.
 * This is necessary since the ESP32-C3 interrupt registers gather both the IN and OUT interrupt status
 * bits in the same register while the generic implementation seperate them into two different registers.
 */
static uint32_t esp32c3_from_int_bitmask(uint32_t in_value, uint32_t out_value)
{
    int i = 0;
    uint32_t r = 0;

    for (i = 0; i < DIM(int_mapping) / 2; i++) {
        if (in_value & int_mapping[i].generic) {
            r |= int_mapping[i].specific;
        }
    }
    for (; i < DIM(int_mapping); i++) {
        if (out_value & int_mapping[i].generic) {
            r |= int_mapping[i].specific;
        }
    }

    return r;
}


/**
 * @brief Get the virtual/generic GDMA register out of an ESP32-C3 GDMA specific interrupt register.
 */
static DmaRegister esp32c3_generic_int_reg(uint32_t reg)
{
    const DmaRegister assoc[] = {
        [R_DMA_INT_RAW]       = GDMA_INT_RAW_REG,
        [R_DMA_INT_ST]        = GDMA_INT_ST_REG,
        [R_DMA_INT_ENA]       = GDMA_INT_ENA_REG,
        [R_DMA_INT_CLR]       = GDMA_INT_CLR_REG,
    };

    /* The `reg` is expressed in bytes while in the array, the entries are expressed in words */
    const uint32_t word = reg / sizeof(uint32_t);
    assert(word < DIM(assoc));

    return assoc[word];
}


/**
 * @brief Get the virtual/generic GDMA register out of an ESP32-C3 GDMA channel configuration register.
 */
static DmaRegister esp32c3_generic_reg(uint32_t reg)
{
    /* The interrupt registers are not part of this table, they must be treated separately */
    const DmaRegister assoc[] = {
        [R_DMA_CONF0]         = GDMA_CONF0_REG,
        [R_DMA_CONF1]         = GDMA_CONF1_REG,
        [R_DMA_FIFO_ST]       = GDMA_FIFO_ST_REG,
        [R_DMA_POP]           = GDMA_POP_REG,
        [R_DMA_LINK]          = GDMA_LINK_REG,
        [R_DMA_STATE]         = GDMA_STATE_REG,
        [R_DMA_SUC_EOF_DESC]  = GDMA_SUC_EOF_DESC_REG,
        [R_DMA_ERR_EOF_DESC]  = GDMA_ERR_EOF_DESC_REG,
        [R_DMA_DESC_ADDR]     = GDMA_DESC_ADDR_REG,
        [R_DMA_BF0_DESC_ADDR] = GDMA_BF0_DESC_ADDR_REG,
        [R_DMA_BF1_DESC_ADDR] = GDMA_BF1_DESC_ADDR_REG,
        [R_DMA_PRIORITY]      = GDMA_PRIORITY_REG,
        [R_DMA_PERI_SEL]      = GDMA_PERI_SEL_REG,
    };

    /* The `reg` is expressed in bytes while in the array, the entries are expressed in words */
    const uint32_t word = reg / sizeof(uint32_t);
    if (word >= DIM(assoc)) {
        return GDMA_UNKNOWN;
    }

    return assoc[word];
}


static uint32_t esp32c3_read_int_register(ESP32C3GdmaState *s, hwaddr addr)
{
    /* The interrupt registers are packed 4 per channel (RAW/ST/ENA/CLR), so the
     * per-channel stride is DMA_INT_CHAN_REGS_SIZE (0x10), NOT DMA_DIR_REGS_SIZE
     * (which is the much larger per-direction channel-config stride). Using the
     * wrong stride keeps chan stuck at 0 and lets reg run past the 4-entry table
     * for channels >= 1 (e.g. when SPI2 and I2S occupy different GDMA channels). */
    const uint32_t chan = addr / DMA_INT_CHAN_REGS_SIZE;
    const uint32_t reg  = addr % DMA_INT_CHAN_REGS_SIZE;
    const uint32_t generic_reg = esp32c3_generic_int_reg(reg);

    uint32_t in_value  = esp_gdma_read_chan_register(&s->parent, ESP_GDMA_IN_IDX, chan, generic_reg);
    uint32_t out_value = esp_gdma_read_chan_register(&s->parent, ESP_GDMA_OUT_IDX, chan, generic_reg);
    return esp32c3_from_int_bitmask(in_value, out_value);
}


static uint32_t esp32c3_read_chan_register(ESP32C3GdmaState *s, hwaddr addr)
{
    /* Check which channel and which direction is being written to */
    const uint32_t chan_with_dir = addr / DMA_DIR_REGS_SIZE;
    const uint32_t chan = chan_with_dir >> 1;
    const uint32_t dir = (chan_with_dir & 1) ? ESP_GDMA_OUT_IDX : ESP_GDMA_IN_IDX;
    const uint32_t reg = addr % DMA_DIR_REGS_SIZE;

    return esp_gdma_read_chan_register(&s->parent, dir, chan, esp32c3_generic_reg(reg));
}


static uint64_t esp32c3_gdma_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(opaque);
    ESPGdmaState *parent = &s->parent;
    uint64_t r = 0;


    /* The registers on the ESP32-C3 start with the interrupt registers, then come the misc registers, and
     * finally the channels configurtaion registers */
    switch (addr) {
        case DMA_INT_CHAN_REGS_START ... DMA_INT_CHAN_REGS_END:
            return esp32c3_read_int_register(s, addr - DMA_INT_CHAN_REGS_START);

        case A_DMA_MISC_CONF:
            return esp_gdma_read_register(parent, GDMA_MISC_REG);

        case DMA_CHAN_REGS_START ... DMA_CHAN_REGS_END:
            return esp32c3_read_chan_register(s, addr - DMA_CHAN_REGS_START);

        default:
        /* Unknown or dummy register */
#if ESP32C3_GDMA_WARNING
        warn_report("[C3][GDMA] Unsupported read to %08lx", addr);
#endif
            break;
    }

#if ESP32C3_GDMA_DEBUG
    info_report("[C3][GDMA] Reading from %08lx (%08lx)", addr, r);
#endif

    return r;
}


static void esp32c3_write_int_register(ESP32C3GdmaState *s, hwaddr addr, uint32_t value)
{
    /* Interrupt registers are packed 4 per channel; use the 0x10 int stride
     * (see esp32c3_read_int_register for the rationale). */
    const uint32_t chan = addr / DMA_INT_CHAN_REGS_SIZE;
    const uint32_t reg  = addr % DMA_INT_CHAN_REGS_SIZE;
    uint32_t in_value = 0;
    uint32_t out_value = 0;

    /* For each direction, convert the C3 interrupt bitmask into a generic bitmask */
    esp32c3_to_int_bitmask(value, &in_value, &out_value);
    esp_gdma_write_chan_register(&s->parent, ESP_GDMA_IN_IDX, chan, esp32c3_generic_int_reg(reg), in_value);
    esp_gdma_write_chan_register(&s->parent, ESP_GDMA_OUT_IDX, chan, esp32c3_generic_int_reg(reg), out_value);
}


static void esp32c3_write_chan_register(ESP32C3GdmaState *s, hwaddr addr, uint32_t value)
{
    /* Check which channel and which direction is being written to */
    const uint32_t chan_with_dir = addr / DMA_DIR_REGS_SIZE;
    const uint32_t chan = chan_with_dir >> 1;
    const uint32_t dir = (chan_with_dir & 1) ? ESP_GDMA_OUT_IDX : ESP_GDMA_IN_IDX;
    const uint32_t reg = addr % DMA_DIR_REGS_SIZE;

    esp_gdma_write_chan_register(&s->parent, dir, chan, esp32c3_generic_reg(reg), value);
}


static void esp32c3_gdma_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned int size)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(opaque);
    ESPGdmaState *parent = &s->parent;

#if ESP32C3_GDMA_DEBUG
    info_report("[C3][GDMA] Writing to %08lx (%08lx)", addr, value);
#endif

    /* The registers on the ESP32-C3 start with the interrupt registers, then come the misc registers, and
     * finally the channels configurtaion registers */
    switch (addr) {
        case DMA_INT_CHAN_REGS_START ... DMA_INT_CHAN_REGS_END:
            esp32c3_write_int_register(s, addr - DMA_INT_CHAN_REGS_START, (uint32_t) value);
            break;

        case A_DMA_MISC_CONF:
            esp_gdma_write_register(parent, GDMA_MISC_REG, value);
            break;

        case DMA_CHAN_REGS_START ... DMA_CHAN_REGS_END:
            esp32c3_write_chan_register(s, addr - DMA_CHAN_REGS_START, (uint32_t) value);
            break;

        default:
        /* Unknown or dummy register */
#if ESP32C3_GDMA_WARNING
        warn_report("[C3][GDMA] Unsupported write to %08lx (%08lx)", addr, value);
#endif
            break;
    }
}


static const MemoryRegionOps esp_gdma_ops = {
    .read =  esp32c3_gdma_read,
    .write = esp32c3_gdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp32c3_gdma_init(Object *obj)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_gdma_ops, s,
                          TYPE_ESP32C3_GDMA, ESP32C3_GDMA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

/**
 * @brief Check if the given GDMA peripheral is reserved on the ESP32-C3
 */
static bool esp32c3_is_periph_invalid(ESPGdmaState *s, GdmaPeripheral per)
{
    (void) s;
    /* On the ESP32-C3, some peripherals don't exist, mark them as reserved */
    return per == GDMA_SPI3 || per == GDMA_I2S1 || per == GDMA_LCDCAM;
}

static void esp32c3_gdma_class_init(ObjectClass *klass, void *data)
{
    ESPGdmaClass* class = ESP_GDMA_CLASS(klass);
    class->is_periph_invalid = esp32c3_is_periph_invalid;
    class->m_channel_count = ESP32C3_GDMA_CHANNEL_COUNT;
}


static const TypeInfo esp32c3_gdma_info = {
    .name = TYPE_ESP32C3_GDMA,
    .parent = TYPE_ESP_GDMA,
    .instance_size = sizeof(ESP32C3GdmaState),
    .instance_init = esp32c3_gdma_init,
    .class_init = esp32c3_gdma_class_init,
    .class_size = sizeof(ESP32C3GdmaClass)
};


static void esp32c3_gdma_register_types(void)
{
    type_register_static(&esp32c3_gdma_info);
}


type_init(esp32c3_gdma_register_types)

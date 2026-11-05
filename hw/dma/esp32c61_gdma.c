/*
 * ESP32-C61 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "hw/dma/esp32c61_gdma.h"

#define C61_GDMA_RAM_ADDR       0x40800000

#define C61_GDMA_IN_INT_START   0x000
#define C61_GDMA_OUT_INT_START  0x030
#define C61_GDMA_INT_REGS_SIZE  0x010
#define C61_GDMA_MISC_CONF      0x064
#define C61_GDMA_DATE           0x068
#define C61_GDMA_CHAN_START     0x070
#define C61_GDMA_DIR_REGS_SIZE  0x060
#define C61_GDMA_LINK_OFFSET    0x010
#define C61_GDMA_IN_LINK_ADDR   0x3AC
#define C61_GDMA_OUT_LINK_ADDR  0x3B8
#define C61_GDMA_REGS_SIZE      0x410

#define C61_GDMA_DATE_VALUE     2410220

#define C61_GDMA_CHAN_END \
    (C61_GDMA_CHAN_START + 2 * ESP32C61_GDMA_CHANNEL_COUNT * C61_GDMA_DIR_REGS_SIZE)

/* Bit positions in the IN and OUT LINK registers */
#define C61_GDMA_IN_AUTO_RET    0
#define C61_GDMA_IN_STOP        1
#define C61_GDMA_OUT_STOP       0

static DmaRegister esp32c61_gdma_int_reg(hwaddr offset)
{
    static const DmaRegister regs[] = {
        GDMA_INT_RAW_REG, GDMA_INT_ST_REG, GDMA_INT_ENA_REG, GDMA_INT_CLR_REG,
    };
    return regs[offset / 4];
}

static DmaRegister esp32c61_gdma_chan_reg(hwaddr offset)
{
    static const DmaRegister regs[] = {
        GDMA_CONF0_REG, GDMA_CONF1_REG, GDMA_FIFO_ST_REG, GDMA_POP_REG,
        GDMA_LINK_REG, GDMA_STATE_REG, GDMA_SUC_EOF_DESC_REG, GDMA_ERR_EOF_DESC_REG,
        GDMA_DESC_ADDR_REG, GDMA_BF0_DESC_ADDR_REG, GDMA_BF1_DESC_ADDR_REG,
        GDMA_PRIORITY_REG, GDMA_PERI_SEL_REG,
    };
    const uint32_t word = offset / 4;
    return word < ARRAY_SIZE(regs) ? regs[word] : GDMA_UNKNOWN;
}

/*
 * The generic model takes the low bits of the descriptor address and the
 * control bits from one LINK register. This chip has them in separate registers
 * and at different bit positions, so combine them on each LINK write.
 */
static uint32_t esp32c61_gdma_generic_link(ESP32C61GdmaState *s, uint32_t dir, uint32_t chan,
                                           uint32_t value)
{
    const uint32_t addr = s->link_addr[dir][chan] & R_GDMA_OUT_LINK_ADDR_MASK;

    if (dir == ESP_GDMA_IN_IDX) {
        return addr | ((value >> C61_GDMA_IN_AUTO_RET) & 1) << R_GDMA_IN_LINK_AUTO_RET_SHIFT
                    | ((value >> C61_GDMA_IN_STOP) & 7) << R_GDMA_IN_LINK_STOP_SHIFT;
    }
    return addr | ((value >> C61_GDMA_OUT_STOP) & 7) << R_GDMA_OUT_LINK_STOP_SHIFT;
}

/* Transfers run synchronously, so the PARK bit always reads as idle. */
static uint32_t esp32c61_gdma_read_link(ESP32C61GdmaState *s, uint32_t dir, uint32_t chan)
{
    if (dir == ESP_GDMA_IN_IDX) {
        const uint32_t link = esp_gdma_read_chan_register(&s->parent, dir, chan, GDMA_LINK_REG);
        return BIT(4) | FIELD_EX32(link, GDMA_IN_LINK, AUTO_RET);
    }
    return BIT(3);
}

static uint64_t esp32c61_gdma_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C61GdmaState *s = ESP32C61_GDMA(opaque);
    const uint32_t chans = ESP32C61_GDMA_CHANNEL_COUNT;

    if (addr < C61_GDMA_IN_INT_START + chans * C61_GDMA_INT_REGS_SIZE) {
        return esp_gdma_read_chan_register(&s->parent, ESP_GDMA_IN_IDX,
                                           addr / C61_GDMA_INT_REGS_SIZE,
                                           esp32c61_gdma_int_reg(addr % C61_GDMA_INT_REGS_SIZE));
    } else if (addr >= C61_GDMA_OUT_INT_START &&
               addr < C61_GDMA_OUT_INT_START + chans * C61_GDMA_INT_REGS_SIZE) {
        const hwaddr off = addr - C61_GDMA_OUT_INT_START;
        return esp_gdma_read_chan_register(&s->parent, ESP_GDMA_OUT_IDX,
                                           off / C61_GDMA_INT_REGS_SIZE,
                                           esp32c61_gdma_int_reg(off % C61_GDMA_INT_REGS_SIZE));
    } else if (addr == C61_GDMA_MISC_CONF) {
        return esp_gdma_read_register(&s->parent, GDMA_MISC_REG);
    } else if (addr == C61_GDMA_DATE) {
        return C61_GDMA_DATE_VALUE;
    } else if (addr >= C61_GDMA_CHAN_START && addr < C61_GDMA_CHAN_END) {
        const hwaddr off = addr - C61_GDMA_CHAN_START;
        const uint32_t chan_with_dir = off / C61_GDMA_DIR_REGS_SIZE;
        const uint32_t chan = chan_with_dir >> 1;
        const uint32_t dir = (chan_with_dir & 1) ? ESP_GDMA_OUT_IDX : ESP_GDMA_IN_IDX;
        const hwaddr reg = off % C61_GDMA_DIR_REGS_SIZE;

        if (reg == C61_GDMA_LINK_OFFSET) {
            return esp32c61_gdma_read_link(s, dir, chan);
        }
        return esp_gdma_read_chan_register(&s->parent, dir, chan, esp32c61_gdma_chan_reg(reg));
    } else if (addr >= C61_GDMA_IN_LINK_ADDR && addr < C61_GDMA_IN_LINK_ADDR + chans * 4) {
        return s->link_addr[ESP_GDMA_IN_IDX][(addr - C61_GDMA_IN_LINK_ADDR) / 4];
    } else if (addr >= C61_GDMA_OUT_LINK_ADDR && addr < C61_GDMA_OUT_LINK_ADDR + chans * 4) {
        return s->link_addr[ESP_GDMA_OUT_IDX][(addr - C61_GDMA_OUT_LINK_ADDR) / 4];
    }
    return 0;
}

static void esp32c61_gdma_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESP32C61GdmaState *s = ESP32C61_GDMA(opaque);
    const uint32_t chans = ESP32C61_GDMA_CHANNEL_COUNT;

    if (addr < C61_GDMA_IN_INT_START + chans * C61_GDMA_INT_REGS_SIZE) {
        esp_gdma_write_chan_register(&s->parent, ESP_GDMA_IN_IDX,
                                     addr / C61_GDMA_INT_REGS_SIZE,
                                     esp32c61_gdma_int_reg(addr % C61_GDMA_INT_REGS_SIZE), value);
    } else if (addr >= C61_GDMA_OUT_INT_START &&
               addr < C61_GDMA_OUT_INT_START + chans * C61_GDMA_INT_REGS_SIZE) {
        const hwaddr off = addr - C61_GDMA_OUT_INT_START;
        esp_gdma_write_chan_register(&s->parent, ESP_GDMA_OUT_IDX,
                                     off / C61_GDMA_INT_REGS_SIZE,
                                     esp32c61_gdma_int_reg(off % C61_GDMA_INT_REGS_SIZE), value);
    } else if (addr == C61_GDMA_MISC_CONF) {
        esp_gdma_write_register(&s->parent, GDMA_MISC_REG, value);
    } else if (addr >= C61_GDMA_CHAN_START && addr < C61_GDMA_CHAN_END) {
        const hwaddr off = addr - C61_GDMA_CHAN_START;
        const uint32_t chan_with_dir = off / C61_GDMA_DIR_REGS_SIZE;
        const uint32_t chan = chan_with_dir >> 1;
        const uint32_t dir = (chan_with_dir & 1) ? ESP_GDMA_OUT_IDX : ESP_GDMA_IN_IDX;
        const hwaddr reg = off % C61_GDMA_DIR_REGS_SIZE;

        if (reg == C61_GDMA_LINK_OFFSET) {
            value = esp32c61_gdma_generic_link(s, dir, chan, value);
        }
        esp_gdma_write_chan_register(&s->parent, dir, chan, esp32c61_gdma_chan_reg(reg), value);
    } else if (addr >= C61_GDMA_IN_LINK_ADDR && addr < C61_GDMA_IN_LINK_ADDR + chans * 4) {
        s->link_addr[ESP_GDMA_IN_IDX][(addr - C61_GDMA_IN_LINK_ADDR) / 4] = value;
    } else if (addr >= C61_GDMA_OUT_LINK_ADDR && addr < C61_GDMA_OUT_LINK_ADDR + chans * 4) {
        s->link_addr[ESP_GDMA_OUT_IDX][(addr - C61_GDMA_OUT_LINK_ADDR) / 4] = value;
    }
}

static const MemoryRegionOps esp32c61_gdma_ops = {
    .read = esp32c61_gdma_read,
    .write = esp32c61_gdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c61_gdma_init(Object *obj)
{
    ESP32C61GdmaState *s = ESP32C61_GDMA(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c61_gdma_ops, s,
                          TYPE_ESP32C61_GDMA, C61_GDMA_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void esp32c61_gdma_reset_hold(Object *obj, ResetType type)
{
    ESP32C61GdmaState *s = ESP32C61_GDMA(obj);
    ESP32C61GdmaClass *klass = ESP32C61_GDMA_GET_CLASS(obj);

    if (klass->parent_phases.hold) {
        klass->parent_phases.hold(obj, type);
    }
    memset(s->link_addr, 0, sizeof(s->link_addr));
}

static void esp32c61_gdma_class_init(ObjectClass *klass, void *data)
{
    ESPGdmaClass *gdma = ESP_GDMA_CLASS(klass);
    ESP32C61GdmaClass *c61 = ESP32C61_GDMA_CLASS(klass);

    gdma->m_channel_count = ESP32C61_GDMA_CHANNEL_COUNT;
    gdma->ram_addr = C61_GDMA_RAM_ADDR;
    resettable_class_set_parent_phases(RESETTABLE_CLASS(klass), NULL,
                                       esp32c61_gdma_reset_hold, NULL,
                                       &c61->parent_phases);
}

static const TypeInfo esp32c61_gdma_info = {
    .name = TYPE_ESP32C61_GDMA,
    .parent = TYPE_ESP_GDMA,
    .instance_size = sizeof(ESP32C61GdmaState),
    .instance_init = esp32c61_gdma_init,
    .class_init = esp32c61_gdma_class_init,
    .class_size = sizeof(ESP32C61GdmaClass),
};

static void esp32c61_gdma_register_types(void)
{
    type_register_static(&esp32c61_gdma_info);
}

type_init(esp32c61_gdma_register_types)

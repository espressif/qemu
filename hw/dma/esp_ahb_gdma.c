/*
 * GDMA of the chips with the AHB_DMA register block (ESP32-C5, ESP32-C61)
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
#include "hw/dma/esp_ahb_gdma.h"

#define AHB_GDMA_RAM_ADDR       0x40800000
#define AHB_GDMA_DATE_VALUE     2410220

/* Bit positions in the IN and OUT LINK registers */
#define AHB_GDMA_IN_AUTO_RET    0
#define AHB_GDMA_IN_STOP        1
#define AHB_GDMA_OUT_STOP       0

#define AHB_GDMA_INT_REGS_SIZE  0x010

static const DmaRegister esp_ahb_gdma_chan_regs[] = {
    GDMA_CONF0_REG, GDMA_CONF1_REG, GDMA_FIFO_ST_REG, GDMA_POP_REG,
    GDMA_LINK_REG, GDMA_STATE_REG, GDMA_SUC_EOF_DESC_REG, GDMA_ERR_EOF_DESC_REG,
    GDMA_DESC_ADDR_REG, GDMA_BF0_DESC_ADDR_REG, GDMA_BF1_DESC_ADDR_REG,
    GDMA_PRIORITY_REG, GDMA_PERI_SEL_REG,
};

/* Register block of ESP32-C5 and ESP32-C61 */
static const ESPAhbGdmaLayout esp_ahb_gdma_default_layout = {
    .out_int_start = 0x030,
    .misc_conf = 0x064,
    .date = 0x068,
    .chan_start = 0x070,
    .chan_size = 0x0C0,
    .out_offset = 0x060,
    .link_offset = 0x010,
    .in_link_addr = 0x3AC,
    .out_link_addr = 0x3B8,
    .regs_size = 0x418,
    .chan_regs = esp_ahb_gdma_chan_regs,
    .chan_reg_count = ARRAY_SIZE(esp_ahb_gdma_chan_regs),
};

static DmaRegister esp_ahb_gdma_int_reg(hwaddr offset)
{
    static const DmaRegister regs[] = {
        GDMA_INT_RAW_REG, GDMA_INT_ST_REG, GDMA_INT_ENA_REG, GDMA_INT_CLR_REG,
    };
    return regs[offset / 4];
}

/*
 * The generic model takes the low bits of the descriptor address and the
 * control bits from one LINK register. These chips have them in separate registers
 * and at different bit positions, so combine them on each LINK write.
 */
static uint32_t esp_ahb_gdma_generic_link(ESPAhbGdmaState *s, uint32_t dir, uint32_t chan,
                                           uint32_t value)
{
    const uint32_t addr = s->link_addr[dir][chan] & R_GDMA_OUT_LINK_ADDR_MASK;

    if (dir == ESP_GDMA_IN_IDX) {
        return addr | ((value >> AHB_GDMA_IN_AUTO_RET) & 1) << R_GDMA_IN_LINK_AUTO_RET_SHIFT
                    | ((value >> AHB_GDMA_IN_STOP) & 7) << R_GDMA_IN_LINK_STOP_SHIFT;
    }
    return addr | ((value >> AHB_GDMA_OUT_STOP) & 7) << R_GDMA_OUT_LINK_STOP_SHIFT;
}

/* Transfers run synchronously, so the PARK bit always reads as idle. */
static uint32_t esp_ahb_gdma_read_link(ESPAhbGdmaState *s, uint32_t dir, uint32_t chan)
{
    if (dir == ESP_GDMA_IN_IDX) {
        const uint32_t link = esp_gdma_read_chan_register(&s->parent, dir, chan, GDMA_LINK_REG);
        return BIT(4) | FIELD_EX32(link, GDMA_IN_LINK, AUTO_RET);
    }
    return BIT(3);
}

/* Return the storage of a LINK_ADDR register, or NULL if addr is not one. */
static uint32_t *esp_ahb_gdma_link_addr(ESPAhbGdmaState *s, const ESPAhbGdmaLayout *l,
                                        uint32_t chans, hwaddr addr, uint32_t dir,
                                        uint32_t chan, hwaddr reg, bool in_chan_block)
{
    if (in_chan_block) {
        return l->link_addr_offset && reg == l->link_addr_offset ? &s->link_addr[dir][chan] : NULL;
    }
    if (l->in_link_addr && addr >= l->in_link_addr && addr < l->in_link_addr + chans * 4) {
        return &s->link_addr[ESP_GDMA_IN_IDX][(addr - l->in_link_addr) / 4];
    }
    if (l->out_link_addr && addr >= l->out_link_addr && addr < l->out_link_addr + chans * 4) {
        return &s->link_addr[ESP_GDMA_OUT_IDX][(addr - l->out_link_addr) / 4];
    }
    return NULL;
}

/* Split an address in the channel registers into channel, direction and register offset. */
static void esp_ahb_gdma_decode(const ESPAhbGdmaLayout *l, hwaddr addr, uint32_t *chan,
                                uint32_t *dir, hwaddr *reg)
{
    const hwaddr off = addr - l->chan_start;

    *chan = off / l->chan_size;
    *reg = off % l->chan_size;
    *dir = *reg >= l->out_offset ? ESP_GDMA_OUT_IDX : ESP_GDMA_IN_IDX;
    if (*dir == ESP_GDMA_OUT_IDX) {
        *reg -= l->out_offset;
    }
}

static DmaRegister esp_ahb_gdma_chan_reg(const ESPAhbGdmaLayout *l, hwaddr reg)
{
    const uint32_t word = reg / 4;
    return word < l->chan_reg_count ? l->chan_regs[word] : GDMA_UNKNOWN;
}

static uint64_t esp_ahb_gdma_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPAhbGdmaState *s = ESP_AHB_GDMA(opaque);
    const ESPAhbGdmaLayout *l = ESP_AHB_GDMA_GET_CLASS(s)->layout;
    const uint32_t chans = ESP_GDMA_GET_CLASS(s)->m_channel_count;
    const hwaddr chan_end = l->chan_start + chans * l->chan_size;
    uint32_t *link_addr;

    if (addr < chans * AHB_GDMA_INT_REGS_SIZE) {
        return esp_gdma_read_chan_register(&s->parent, ESP_GDMA_IN_IDX,
                                           addr / AHB_GDMA_INT_REGS_SIZE,
                                           esp_ahb_gdma_int_reg(addr % AHB_GDMA_INT_REGS_SIZE));
    } else if (addr >= l->out_int_start &&
               addr < l->out_int_start + chans * AHB_GDMA_INT_REGS_SIZE) {
        const hwaddr off = addr - l->out_int_start;
        return esp_gdma_read_chan_register(&s->parent, ESP_GDMA_OUT_IDX,
                                           off / AHB_GDMA_INT_REGS_SIZE,
                                           esp_ahb_gdma_int_reg(off % AHB_GDMA_INT_REGS_SIZE));
    } else if (addr == l->misc_conf) {
        return esp_gdma_read_register(&s->parent, GDMA_MISC_REG);
    } else if (addr == l->date) {
        return AHB_GDMA_DATE_VALUE;
    } else if (addr >= l->chan_start && addr < chan_end) {
        uint32_t chan, dir;
        hwaddr reg;

        esp_ahb_gdma_decode(l, addr, &chan, &dir, &reg);
        if (reg == l->link_offset) {
            return esp_ahb_gdma_read_link(s, dir, chan);
        }
        link_addr = esp_ahb_gdma_link_addr(s, l, chans, addr, dir, chan, reg, true);
        if (link_addr) {
            return *link_addr;
        }
        return esp_gdma_read_chan_register(&s->parent, dir, chan, esp_ahb_gdma_chan_reg(l, reg));
    }
    link_addr = esp_ahb_gdma_link_addr(s, l, chans, addr, 0, 0, 0, false);
    return link_addr ? *link_addr : 0;
}

static void esp_ahb_gdma_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESPAhbGdmaState *s = ESP_AHB_GDMA(opaque);
    const ESPAhbGdmaLayout *l = ESP_AHB_GDMA_GET_CLASS(s)->layout;
    const uint32_t chans = ESP_GDMA_GET_CLASS(s)->m_channel_count;
    const hwaddr chan_end = l->chan_start + chans * l->chan_size;
    uint32_t *link_addr;

    if (addr < chans * AHB_GDMA_INT_REGS_SIZE) {
        esp_gdma_write_chan_register(&s->parent, ESP_GDMA_IN_IDX,
                                     addr / AHB_GDMA_INT_REGS_SIZE,
                                     esp_ahb_gdma_int_reg(addr % AHB_GDMA_INT_REGS_SIZE), value);
    } else if (addr >= l->out_int_start &&
               addr < l->out_int_start + chans * AHB_GDMA_INT_REGS_SIZE) {
        const hwaddr off = addr - l->out_int_start;
        esp_gdma_write_chan_register(&s->parent, ESP_GDMA_OUT_IDX,
                                     off / AHB_GDMA_INT_REGS_SIZE,
                                     esp_ahb_gdma_int_reg(off % AHB_GDMA_INT_REGS_SIZE), value);
    } else if (addr == l->misc_conf) {
        esp_gdma_write_register(&s->parent, GDMA_MISC_REG, value);
    } else if (addr >= l->chan_start && addr < chan_end) {
        uint32_t chan, dir;
        hwaddr reg;

        esp_ahb_gdma_decode(l, addr, &chan, &dir, &reg);
        link_addr = esp_ahb_gdma_link_addr(s, l, chans, addr, dir, chan, reg, true);
        if (link_addr) {
            *link_addr = value;
            return;
        }
        if (reg == l->link_offset) {
            value = esp_ahb_gdma_generic_link(s, dir, chan, value);
        }
        esp_gdma_write_chan_register(&s->parent, dir, chan, esp_ahb_gdma_chan_reg(l, reg), value);
    } else {
        link_addr = esp_ahb_gdma_link_addr(s, l, chans, addr, 0, 0, 0, false);
        if (link_addr) {
            *link_addr = value;
        }
    }
}

static const MemoryRegionOps esp_ahb_gdma_ops = {
    .read = esp_ahb_gdma_read,
    .write = esp_ahb_gdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp_ahb_gdma_init(Object *obj)
{
    ESPAhbGdmaState *s = ESP_AHB_GDMA(obj);

    memory_region_init_io(&s->iomem, obj, &esp_ahb_gdma_ops, s,
                          TYPE_ESP_AHB_GDMA, ESP_AHB_GDMA_GET_CLASS(s)->layout->regs_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void esp_ahb_gdma_reset_hold(Object *obj, ResetType type)
{
    ESPAhbGdmaState *s = ESP_AHB_GDMA(obj);
    ESPAhbGdmaClass *klass = ESP_AHB_GDMA_GET_CLASS(obj);

    if (klass->parent_phases.hold) {
        klass->parent_phases.hold(obj, type);
    }
    memset(s->link_addr, 0, sizeof(s->link_addr));
}

static void esp_ahb_gdma_class_init(ObjectClass *klass, void *data)
{
    ESPGdmaClass *gdma = ESP_GDMA_CLASS(klass);
    ESPAhbGdmaClass *ahb = ESP_AHB_GDMA_CLASS(klass);

    gdma->ram_addr = AHB_GDMA_RAM_ADDR;
    ahb->layout = &esp_ahb_gdma_default_layout;
    resettable_class_set_parent_phases(RESETTABLE_CLASS(klass), NULL,
                                       esp_ahb_gdma_reset_hold, NULL,
                                       &ahb->parent_phases);
}

static const TypeInfo esp_ahb_gdma_info = {
    .name = TYPE_ESP_AHB_GDMA,
    .parent = TYPE_ESP_GDMA,
    .abstract = true,
    .instance_size = sizeof(ESPAhbGdmaState),
    .instance_init = esp_ahb_gdma_init,
    .class_init = esp_ahb_gdma_class_init,
    .class_size = sizeof(ESPAhbGdmaClass),
};

static void esp_ahb_gdma_register_types(void)
{
    type_register_static(&esp_ahb_gdma_info);
}

type_init(esp_ahb_gdma_register_types)

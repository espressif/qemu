/*
 * ESP32-P4 AXI DMA
 *
 * The register block has all IN channels, then all OUT channels, with the
 * interrupt registers inside each channel. LINK2 holds the descriptor address.
 * The generic model takes the low 20 bits of it, so descriptors must be in
 * internal RAM, and CRC and ETM are not modelled.
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
#include "hw/dma/esp32p4_axi_gdma.h"

#define AXI_GDMA_RAM_ADDR       0x4FF00000
#define AXI_GDMA_DATE_VALUE     36712768

#define AXI_GDMA_CHAN_SIZE      0x68
#define AXI_GDMA_OUT_START      (ESP32P4_AXI_GDMA_CHANNEL_COUNT * AXI_GDMA_CHAN_SIZE)
#define AXI_GDMA_CHANS_END      (2 * AXI_GDMA_OUT_START)
#define AXI_GDMA_RESET_AVAIL    0x28C
#define AXI_GDMA_RESET_AVAIL_END 0x2A4
#define AXI_GDMA_MISC_CONF      0x2A8
#define AXI_GDMA_DATE           0x2D8
#define AXI_GDMA_REGS_SIZE      0x2E0

/* Offsets in a channel's registers */
#define AXI_GDMA_CONF0          0x10
#define AXI_GDMA_LINK1          0x20
#define AXI_GDMA_LINK2          0x24

/* Bit positions in CONF0 and LINK1 */
#define AXI_GDMA_IN_MEM_TRANS   2
#define AXI_GDMA_IN_AUTO_RET    0
#define AXI_GDMA_IN_STOP        1
#define AXI_GDMA_IN_PARK        4
#define AXI_GDMA_OUT_STOP       0
#define AXI_GDMA_OUT_PARK       3

/* Burst sizes are at bit 4 of IN_CONF0 and bit 5 of OUT_CONF0, where the generic model has other fields */
#define AXI_GDMA_IN_CONF0_MASK  (R_GDMA_IN_CONF0_RST_MASK | R_GDMA_IN_CONF0_LOOP_TEST_MASK)
#define AXI_GDMA_OUT_CONF0_MASK (R_GDMA_OUT_CONF0_RST_MASK | R_GDMA_OUT_CONF0_LOOP_TEST_MASK | \
                                 R_GDMA_OUT_CONF0_AUTO_WRBACK_MASK | R_GDMA_OUT_CONF0_EOF_MODE_MASK)

static const DmaRegister axi_gdma_chan_regs[] = {
    GDMA_INT_RAW_REG, GDMA_INT_ST_REG, GDMA_INT_ENA_REG, GDMA_INT_CLR_REG,
    GDMA_CONF0_REG, GDMA_CONF1_REG, GDMA_FIFO_ST_REG, GDMA_POP_REG,
    GDMA_LINK_REG, GDMA_UNKNOWN /* LINK2 */, GDMA_STATE_REG, GDMA_SUC_EOF_DESC_REG,
    GDMA_ERR_EOF_DESC_REG, GDMA_DESC_ADDR_REG, GDMA_BF0_DESC_ADDR_REG,
    GDMA_BF1_DESC_ADDR_REG, GDMA_PRIORITY_REG, GDMA_PERI_SEL_REG,
};

static void axi_gdma_decode(hwaddr addr, uint32_t *dir, uint32_t *chan, hwaddr *reg)
{
    *dir = addr >= AXI_GDMA_OUT_START ? ESP_GDMA_OUT_IDX : ESP_GDMA_IN_IDX;
    addr -= *dir == ESP_GDMA_OUT_IDX ? AXI_GDMA_OUT_START : 0;
    *chan = addr / AXI_GDMA_CHAN_SIZE;
    *reg = addr % AXI_GDMA_CHAN_SIZE;
}

static DmaRegister axi_gdma_chan_reg(hwaddr reg)
{
    const size_t word = reg / 4;
    return word < ARRAY_SIZE(axi_gdma_chan_regs) ? axi_gdma_chan_regs[word] : GDMA_UNKNOWN;
}

/* Combine LINK2 and the control bits of LINK1 into the generic model's LINK register. */
static uint32_t axi_gdma_generic_link(ESP32P4AxiGdmaState *s, uint32_t dir, uint32_t chan, uint32_t value)
{
    const uint32_t addr = s->link2[dir][chan] & R_GDMA_OUT_LINK_ADDR_MASK;

    if (dir == ESP_GDMA_IN_IDX) {
        return addr | ((value >> AXI_GDMA_IN_AUTO_RET) & 1) << R_GDMA_IN_LINK_AUTO_RET_SHIFT
                    | ((value >> AXI_GDMA_IN_STOP) & 7) << R_GDMA_IN_LINK_STOP_SHIFT;
    }
    return addr | ((value >> AXI_GDMA_OUT_STOP) & 7) << R_GDMA_OUT_LINK_STOP_SHIFT;
}

static uint32_t axi_gdma_generic_conf0(uint32_t dir, uint32_t value)
{
    if (dir == ESP_GDMA_IN_IDX) {
        return (value & AXI_GDMA_IN_CONF0_MASK) |
               ((value >> AXI_GDMA_IN_MEM_TRANS) & 1) << R_GDMA_IN_CONF0_MEM_TRANS_EN_SHIFT;
    }
    return value & AXI_GDMA_OUT_CONF0_MASK;
}

static uint64_t axi_gdma_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32P4AxiGdmaState *s = ESP32P4_AXI_GDMA(opaque);

    if (addr < AXI_GDMA_CHANS_END) {
        uint32_t dir, chan;
        hwaddr reg;

        axi_gdma_decode(addr, &dir, &chan, &reg);
        switch (reg) {
        case AXI_GDMA_CONF0:
            return s->conf0[dir][chan];
        case AXI_GDMA_LINK2:
            return s->link2[dir][chan];
        case AXI_GDMA_LINK1:
            /* Transfers run synchronously, so the PARK bit always reads as idle. */
            if (dir == ESP_GDMA_IN_IDX) {
                const uint32_t link = esp_gdma_read_chan_register(&s->parent, dir, chan, GDMA_LINK_REG);
                return BIT(AXI_GDMA_IN_PARK) | FIELD_EX32(link, GDMA_IN_LINK, AUTO_RET);
            }
            return BIT(AXI_GDMA_OUT_PARK);
        default:
            return esp_gdma_read_chan_register(&s->parent, dir, chan, axi_gdma_chan_reg(reg));
        }
    } else if (addr >= AXI_GDMA_RESET_AVAIL && addr < AXI_GDMA_RESET_AVAIL_END) {
        return 1;
    } else if (addr == AXI_GDMA_MISC_CONF) {
        return esp_gdma_read_register(&s->parent, GDMA_MISC_REG);
    } else if (addr == AXI_GDMA_DATE) {
        return AXI_GDMA_DATE_VALUE;
    }
    return 0;
}

static void axi_gdma_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESP32P4AxiGdmaState *s = ESP32P4_AXI_GDMA(opaque);

    if (addr < AXI_GDMA_CHANS_END) {
        uint32_t dir, chan;
        hwaddr reg;

        axi_gdma_decode(addr, &dir, &chan, &reg);
        switch (reg) {
        case AXI_GDMA_LINK2:
            s->link2[dir][chan] = value;
            return;
        case AXI_GDMA_LINK1:
            value = axi_gdma_generic_link(s, dir, chan, value);
            break;
        case AXI_GDMA_CONF0:
            s->conf0[dir][chan] = value;
            value = axi_gdma_generic_conf0(dir, value);
            break;
        default:
            break;
        }
        esp_gdma_write_chan_register(&s->parent, dir, chan, axi_gdma_chan_reg(reg), value);
    } else if (addr == AXI_GDMA_MISC_CONF) {
        esp_gdma_write_register(&s->parent, GDMA_MISC_REG, value);
    }
}

static const MemoryRegionOps axi_gdma_ops = {
    .read = axi_gdma_read,
    .write = axi_gdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* SOC_GDMA_TRIG_PERIPH_AES0 and SHA0 on this chip. Only AES and SHA use the lookup here. */
static uint32_t axi_gdma_periph_sel(GdmaPeripheral per)
{
    return per == GDMA_AES ? 4 : per == GDMA_SHA ? 5 : per;
}

static void axi_gdma_init(Object *obj)
{
    ESP32P4AxiGdmaState *s = ESP32P4_AXI_GDMA(obj);

    memory_region_init_io(&s->iomem, obj, &axi_gdma_ops, s, TYPE_ESP32P4_AXI_GDMA, AXI_GDMA_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void axi_gdma_reset_hold(Object *obj, ResetType type)
{
    ESP32P4AxiGdmaState *s = ESP32P4_AXI_GDMA(obj);
    ESP32P4AxiGdmaClass *klass = ESP32P4_AXI_GDMA_GET_CLASS(obj);

    if (klass->parent_phases.hold) {
        klass->parent_phases.hold(obj, type);
    }
    memset(s->link2, 0, sizeof(s->link2));
    memset(s->conf0, 0, sizeof(s->conf0));
}

static void axi_gdma_class_init(ObjectClass *klass, void *data)
{
    ESPGdmaClass *gdma = ESP_GDMA_CLASS(klass);
    ESP32P4AxiGdmaClass *axi = ESP32P4_AXI_GDMA_CLASS(klass);

    gdma->m_channel_count = ESP32P4_AXI_GDMA_CHANNEL_COUNT;
    gdma->ram_addr = AXI_GDMA_RAM_ADDR;
    gdma->periph_sel = axi_gdma_periph_sel;
    resettable_class_set_parent_phases(RESETTABLE_CLASS(klass), NULL, axi_gdma_reset_hold, NULL,
                                       &axi->parent_phases);
}

static const TypeInfo axi_gdma_info = {
    .name = TYPE_ESP32P4_AXI_GDMA,
    .parent = TYPE_ESP_GDMA,
    .instance_size = sizeof(ESP32P4AxiGdmaState),
    .instance_init = axi_gdma_init,
    .class_init = axi_gdma_class_init,
    .class_size = sizeof(ESP32P4AxiGdmaClass),
};

static void axi_gdma_register_types(void)
{
    type_register_static(&axi_gdma_info);
}

type_init(axi_gdma_register_types)

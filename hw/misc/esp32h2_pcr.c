/*
 * ESP32-H2 PCR (Peripheral Clock and Reset) controller emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32h2_pcr.h"
#include <stdlib.h>
#include <time.h>

#define PCR_UART0_CONF_REG          0x000
#define PCR_UART1_CONF_REG          0x00c
#define PCR_MSPI_CONF_REG           0x018
#define PCR_MSPI_CLK_CONF_REG       0x01c
#define PCR_I2C0_CONF_REG           0x020
#define PCR_I2C1_CONF_REG           0x028
#define PCR_UHCI_CONF_REG           0x030
#define PCR_RMT_CONF_REG            0x034
#define PCR_LEDC_CONF_REG           0x03c
#define PCR_TIMERGROUP0_CONF_REG    0x044
#define PCR_TIMERGROUP1_CONF_REG    0x050
#define PCR_SYSTIMER_CONF_REG       0x05c
#define PCR_TWAI0_CONF_REG          0x064
#define PCR_I2S_CONF_REG            0x06c
#define PCR_SARADC_CONF_REG         0x080
#define PCR_USB_DEVICE_CONF_REG     0x08c
#define PCR_INTMTX_CONF_REG        0x090
#define PCR_PCNT_CONF_REG           0x094
#define PCR_ETM_CONF_REG            0x098
#define PCR_PWM_CONF_REG            0x09c
#define PCR_PARL_IO_CONF_REG        0x0a4
#define PCR_PVT_MONITOR_CONF_REG    0x0b0
#define A_SYSCON_RND_DATA_REG       0x0b0
#define PCR_GDMA_CONF_REG           0x0b8
#define PCR_SPI2_CONF_REG           0x0bc
#define PCR_AES_CONF_REG            0x0c4
#define PCR_SHA_CONF_REG            0x0c8
#define PCR_RSA_CONF_REG            0x0cc
#define PCR_ECC_CONF_REG            0x0d4
#define PCR_DS_CONF_REG             0x0dc
#define PCR_HMAC_CONF_REG           0x0e0
#define PCR_ECDSA_CONF_REG          0x0e4
#define PCR_IOMUX_CONF_REG          0x0e8
#define PCR_MEM_MONITOR_CONF_REG    0x0f0
#define PCR_REGDMA_CONF_REG         0x0f4
#define PCR_TRACE_CONF_REG          0x0f8
#define PCR_ASSIST_CONF_REG         0x0fc
#define PCR_CACHE_CONF_REG          0x100
#define PCR_MODEM_CONF_REG          0x104
#define PCR_TIMEOUT_CONF_REG        0x108
#define PCR_SYSCLK_CONF_REG         0x10c
#define PCR_BUS_CLK_UPDATE_REG      0x148
#define PCR_RND_DATA_REG            0x150
#define A_SYSCON_ORIGIN_REG         0x3F8

/*
 * Each CONF register has RO READY bits that default to 1.
 * We store {offset, default_value, ready_mask} for registers with non-trivial defaults.
 * The ready_mask bits are RO and always read as 1.
 */
typedef struct {
    uint16_t offset;
    uint32_t default_val;
    uint32_t ready_mask;
} PcrConfReg;

static const PcrConfReg pcr_conf_regs[] = {
    /* Standard CONF regs: bit 0=CLK_EN(1), bit 2=READY(1) */
    { PCR_UART0_CONF_REG,       0x05, BIT(2) },
    { PCR_UART1_CONF_REG,       0x05, BIT(2) },
    { PCR_I2C0_CONF_REG,        0x05, BIT(2) },
    { PCR_I2C1_CONF_REG,        0x05, BIT(2) },
    { PCR_UHCI_CONF_REG,        0x05, BIT(2) },
    { PCR_RMT_CONF_REG,         0x05, BIT(2) },
    { PCR_LEDC_CONF_REG,        0x05, BIT(2) },
    { PCR_SYSTIMER_CONF_REG,    0x05, BIT(2) },
    { PCR_TWAI0_CONF_REG,       0x05, BIT(2) },
    { PCR_SARADC_CONF_REG,      0x05, BIT(2) },
    { PCR_USB_DEVICE_CONF_REG,  0x05, BIT(2) },
    { PCR_INTMTX_CONF_REG,      0x05, BIT(2) },
    { PCR_PCNT_CONF_REG,        0x05, BIT(2) },
    { PCR_ETM_CONF_REG,         0x05, BIT(2) },
    { PCR_PWM_CONF_REG,         0x05, BIT(2) },
    { PCR_PARL_IO_CONF_REG,     0x05, BIT(2) },
    { PCR_PVT_MONITOR_CONF_REG, 0x05, BIT(2) },
    { PCR_GDMA_CONF_REG,        0x05, BIT(2) },
    { PCR_SPI2_CONF_REG,        0x05, BIT(2) },
    { PCR_AES_CONF_REG,         0x05, BIT(2) },
    { PCR_SHA_CONF_REG,         0x05, BIT(2) },
    { PCR_RSA_CONF_REG,         0x05, BIT(2) },
    { PCR_ECC_CONF_REG,         0x05, BIT(2) },
    { PCR_DS_CONF_REG,          0x05, BIT(2) },
    { PCR_HMAC_CONF_REG,        0x05, BIT(2) },
    { PCR_ECDSA_CONF_REG,       0x05, BIT(2) },
    { PCR_MEM_MONITOR_CONF_REG, 0x05, BIT(2) },
    /* MSPI: bit 0=CLK_EN(1), bit 2=PLL_CLK_EN(1), bit 5=READY(1) */
    { PCR_MSPI_CONF_REG,        0x25, BIT(5) },
    /* TIMERGROUP: bit 0=CLK_EN(1), bits 2,3,4=WDT_READY,T0_READY,T1_READY(1) */
    { PCR_TIMERGROUP0_CONF_REG, 0x1D, BIT(2) | BIT(3) | BIT(4) },
    { PCR_TIMERGROUP1_CONF_REG, 0x1D, BIT(2) | BIT(3) | BIT(4) },
    /* I2S: bit 0=CLK_EN(1), bits 2,3=RX_READY,TX_READY(1) */
    { PCR_I2S_CONF_REG,         0x0D, BIT(2) | BIT(3) },
};

static const PcrConfReg *find_conf_reg(hwaddr addr)
{
    for (size_t i = 0; i < ARRAY_SIZE(pcr_conf_regs); i++) {
        if (pcr_conf_regs[i].offset == addr) {
            return &pcr_conf_regs[i];
        }
    }
    return NULL;
}

static uint64_t esp32h2_pcr_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H2PcrState *s = ESP32H2_PCR(opaque);

    if (addr >= ESP32H2_PCR_IO_SIZE) {
        return 0;
    }

    switch (addr) {
    case PCR_SYSCLK_CONF_REG:
        /* clk_xtal_freq=32 (bits [30:24]), soc_clk_sel=1 PLL (bits [17:16]),
         * hs_div_num=2 (bits [15:8]) */
        return (32 << 24) | (1 << 16) | (2 << 8);

    case PCR_RND_DATA_REG: {
        static bool rnd_init;
        if (!rnd_init) {
            srand(time(NULL));
            rnd_init = true;
        }
        return rand();
    }

    case A_SYSCON_ORIGIN_REG:
        return 0x51454d55;

    default:
        return s->regs[addr / 4];
    }
}

static void esp32h2_pcr_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    ESP32H2PcrState *s = ESP32H2_PCR(opaque);

    if (addr >= ESP32H2_PCR_IO_SIZE) {
        return;
    }

    uint32_t val = (uint32_t)value;

    if (addr == PCR_BUS_CLK_UPDATE_REG) {
        s->regs[addr / 4] = val & ~BIT(0);
        return;
    }

    const PcrConfReg *cr = find_conf_reg(addr);
    if (cr) {
        val |= cr->ready_mask;
    }

    s->regs[addr / 4] = val;
}

static const MemoryRegionOps esp32h2_pcr_ops = {
    .read  = esp32h2_pcr_read,
    .write = esp32h2_pcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32h2_pcr_init(Object *obj)
{
    ESP32H2PcrState *s = ESP32H2_PCR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32h2_pcr_ops, s,
                          TYPE_ESP32H2_PCR, ESP32H2_PCR_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);

    memset(s->regs, 0, sizeof(s->regs));
    for (size_t i = 0; i < ARRAY_SIZE(pcr_conf_regs); i++) {
        s->regs[pcr_conf_regs[i].offset / 4] = pcr_conf_regs[i].default_val;
    }
}

static void esp32h2_pcr_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32h2_pcr_info = {
    .name = TYPE_ESP32H2_PCR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H2PcrState),
    .instance_init = esp32h2_pcr_init,
    .class_init = esp32h2_pcr_class_init,
};

static void esp32h2_pcr_register_types(void)
{
    type_register_static(&esp32h2_pcr_info);
}

type_init(esp32h2_pcr_register_types)

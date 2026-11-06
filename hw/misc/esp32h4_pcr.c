/*
 * ESP32-H4 PCR (Peripheral Clock and Reset) controller
 *
 * Standalone: SYSCLK_CONF at 0x114, BUS_CLK_UPDATE at 0x148, XTAL 32 MHz.
 * PCR_CORE1_CONF gates HP core1: clock off and reset asserted out of reset.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32h4_pcr.h"
#include "hw/timer/esp_timg.h"
#include <stdlib.h>
#include <time.h>

#define H4_PCR_SYSCLK_CONF_REG     0x114
#define H4_PCR_BUS_CLK_UPDATE_REG  0x148
#define H4_A_SYSCON_RND_DATA_REG   0x0B0
#define H4_A_SYSCON_ORIGIN_REG     0x3F8

#define H4_PCR_MSPI_CLK_CONF_REG   0x01C
#define H4_PCR_INTMTX_CONF_REG     0x098
#define H4_PCR_CPU_FREQ_CONF_REG   0x11C
#define H4_PCR_CORE1_CONF_REG      0x188
#define H4_PCR_CORE1_CLK_EN        (1U << 0)
#define H4_PCR_CORE1_RST_EN        (1U << 1)

#define H4_PCR_RST_EN              (1U << 1)
#define H4_PCR_INTMTX_CORE0_RST_EN (1U << 1)
#define H4_PCR_INTMTX_CORE1_RST_EN (1U << 3)
#define H4_PCR_INTMTX_CORE0_READY  (1U << 4)
#define H4_PCR_INTMTX_CORE1_READY  (1U << 5)

#define H4_PCR_SYSCLK_XTAL_FREQ    (32U << 24)
#define H4_PCR_SYSCLK_XTAL_MASK    (0x7fU << 24)

/* Reset values read from an ESP32-H4 board in ROM download mode */
static const struct {
    hwaddr offset;
    uint32_t value;
} pcr_reset[] = {
    { H4_PCR_MSPI_CLK_CONF_REG, 0x00000400 },
    /* CORE0 intmtx clocked; CORE1 intmtx held in reset. */
    { H4_PCR_INTMTX_CONF_REG,   H4_PCR_INTMTX_CORE1_RST_EN | 0x00000001 },
    { H4_PCR_SYSCLK_CONF_REG,   0xa0000200 },
    /* CORE1_RST_EN comes up asserted and CORE1_CLK_EN stays off until core0
     * releases the core. */
    { H4_PCR_CORE1_CONF_REG,    H4_PCR_CORE1_RST_EN },
};

/* Read-only bits of the CONF registers, set unless the peripheral is in reset */
static uint32_t esp32h4_pcr_ready_mask(hwaddr addr)
{
    switch (addr) {
    case 0x000: /* UART0 */
    case 0x00C: /* UART1 */
    case 0x02C: /* TWAI0 */
    case 0x034: /* UHCI */
    case 0x044: /* LEDC */
    case 0x068: /* SYSTIMER */
    case 0x090: /* USB_DEVICE */
    case 0x0A0: /* ETM */
    case 0x0A4: /* PWM0 */
    case 0x0AC: /* PARL_IO */
    case 0x0C4: /* SPI2 */
    case 0x0CC: /* AES */
    case 0x0D0: /* SHA */
    case 0x0DC: /* ECC */
    case 0x0E8: /* HMAC */
    case 0x0EC: /* ECDSA */
    case 0x164: /* KM */
    case 0x16C: /* TCM_MEM_MONITOR */
    case 0x170: /* PSRAM_MEM_MONITOR */
    case 0x17C: /* USB_OTG11 */
    case 0x194: /* PWM1 */
    case 0x19C: /* SPI3 */
        return 1U << 2;
    case 0x018: /* MSPI */
        return 1U << 3;
    case 0x050: /* TIMERGROUP0 */
    case 0x05C: /* TIMERGROUP1 */
        return (1U << 2) | (1U << 3) | (1U << 4);
    case 0x070: /* I2S */
        return (1U << 2) | (1U << 3);
    case 0x09C: /* PCNT */
        return 1U << 4;
    default:
        return 0;
    }
}

static void esp32h4_pcr_update_cpu_freq(ESP32H4PcrState *s)
{
    /* XTAL, RC_FAST, XTAL_X2, PLL */
    static const uint32_t soc_clk_hz[] = {
        32000000, ESP_RC_FAST_FREQ, 64000000, 96000000,
    };
    const uint32_t sel = extract32(s->reg[H4_PCR_SYSCLK_CONF_REG / 4], 16, 2);
    const uint32_t div = extract32(s->reg[H4_PCR_CPU_FREQ_CONF_REG / 4], 0, 8) + 1;

    if (s->cpu_freq_changed) {
        s->cpu_freq_changed(s->cpu_freq_opaque, soc_clk_hz[sel] / div);
    }
}

static bool esp32h4_core1_released(ESP32H4PcrState *s)
{
    const uint32_t conf = s->reg[H4_PCR_CORE1_CONF_REG / 4];

    return (conf & H4_PCR_CORE1_CLK_EN) && !(conf & H4_PCR_CORE1_RST_EN);
}

static uint64_t esp32h4_pcr_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H4PcrState *s = ESP32H4_PCR(opaque);

    switch (addr) {
    case H4_A_SYSCON_RND_DATA_REG:
        {
            static bool init;
            if (!init) {
                srand(time(NULL));
                init = true;
            }
            return rand();
        }
    case H4_A_SYSCON_ORIGIN_REG:
        return 0x51454d55;
    default:
        break;
    }

    if (addr < ESP32H4_PCR_IO_SIZE) {
        uint32_t val = s->reg[addr / 4];
        const uint32_t ready = esp32h4_pcr_ready_mask(addr);

        if (addr == H4_PCR_INTMTX_CONF_REG) {
            val &= ~(H4_PCR_INTMTX_CORE0_READY | H4_PCR_INTMTX_CORE1_READY);
            if (!(val & H4_PCR_INTMTX_CORE0_RST_EN)) {
                val |= H4_PCR_INTMTX_CORE0_READY;
            }
            if (!(val & H4_PCR_INTMTX_CORE1_RST_EN)) {
                val |= H4_PCR_INTMTX_CORE1_READY;
            }
            return val;
        }

        val &= ~ready;
        if (!(val & H4_PCR_RST_EN)) {
            val |= ready;
        }
        return val;
    }

    return 0;
}

static void esp32h4_pcr_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32H4PcrState *s = ESP32H4_PCR(opaque);

    if (addr < ESP32H4_PCR_IO_SIZE) {
        s->reg[addr / 4] = (uint32_t)value;

        if (addr == H4_PCR_BUS_CLK_UPDATE_REG) {
            s->reg[addr / 4] &= ~1;
        } else if (addr == H4_PCR_SYSCLK_CONF_REG) {
            s->reg[addr / 4] &= ~H4_PCR_SYSCLK_XTAL_MASK;
            s->reg[addr / 4] |= H4_PCR_SYSCLK_XTAL_FREQ;
            esp32h4_pcr_update_cpu_freq(s);
        } else if (addr == H4_PCR_CPU_FREQ_CONF_REG) {
            esp32h4_pcr_update_cpu_freq(s);
        }

        if (addr == H4_PCR_CORE1_CONF_REG) {
            qemu_set_irq(s->core1_run, esp32h4_core1_released(s) ? 1 : 0);
        }
    }
}

static void esp32h4_pcr_reset_hold(Object *obj, ResetType type)
{
    ESP32H4PcrState *s = ESP32H4_PCR(obj);

    memset(s->reg, 0, sizeof(s->reg));
    for (size_t i = 0; i < ARRAY_SIZE(pcr_reset); i++) {
        s->reg[pcr_reset[i].offset / 4] = pcr_reset[i].value;
    }

    qemu_set_irq(s->core1_run, esp32h4_core1_released(s) ? 1 : 0);
    esp32h4_pcr_update_cpu_freq(s);
}

static const MemoryRegionOps esp32h4_pcr_ops = {
    .read  = esp32h4_pcr_read,
    .write = esp32h4_pcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32h4_pcr_init(Object *obj)
{
    ESP32H4PcrState *s = ESP32H4_PCR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32h4_pcr_ops, s,
                          TYPE_ESP32H4_PCR, ESP32H4_PCR_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_out_named(DEVICE(s), &s->core1_run,
                             ESP32H4_PCR_CORE1_RUN_GPIO, 1);
}

static void esp32h4_pcr_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32h4_pcr_reset_hold;
}

static const TypeInfo esp32h4_pcr_info = {
    .name = TYPE_ESP32H4_PCR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H4PcrState),
    .instance_init = esp32h4_pcr_init,
    .class_init = esp32h4_pcr_class_init,
};

static void esp32h4_pcr_register_types(void)
{
    type_register_static(&esp32h4_pcr_info);
}
type_init(esp32h4_pcr_register_types)

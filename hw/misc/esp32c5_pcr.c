/*
 * ESP32-C5 PCR (Peripheral Clock and Reset) controller emulation
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
#include "hw/misc/esp32c5_pcr.h"
#include "hw/timer/esp_timg.h"
#include <stdlib.h>
#include <time.h>

#define A_SYSCON_RND_DATA_REG   0x0B0
#define PCR_SYSCLK_CONF_REG     0x110
#define PCR_CPU_FREQ_CONF_REG   0x118
#define PCR_MSPI_CLK_CONF_REG   0x01C
#define PCR_BUS_CLK_UPDATE_REG  0x144
#define A_SYSCON_ORIGIN_REG     0x3F8

#define PCR_RST_EN              (1 << 1)

#define PCR_MSPI_FUNC_CLK_EN        (1 << 10)
#define PCR_SYSCLK_HS_DIV_NUM_RESET (2 << 8)
#define PCR_SYSCLK_XTAL_FREQ_MHZ    (48 << 24)
#define PCR_SYSCLK_XTAL_FREQ_MASK   (0x7fu << 24)

/*
 * Read-only bits of the peripheral CONF registers that are set unless the
 * peripheral is held in reset. Other PCR registers have ordinary fields at
 * these positions, such as the CPU clock divider.
 */
static uint32_t esp32c5_pcr_ready_mask(hwaddr addr)
{
    switch (addr) {
        case 0x000: /* UART0 */
        case 0x00C: /* UART1 */
        case 0x028: /* TWAI0 */
        case 0x030: /* TWAI1 */
        case 0x038: /* UHCI */
        case 0x048: /* LEDC */
        case 0x06C: /* SYSTIMER */
        case 0x094: /* USB_DEVICE */
        case 0x098: /* INTMTX */
        case 0x09C: /* PCNT */
        case 0x0A0: /* ETM */
        case 0x0A4: /* PWM */
        case 0x0AC: /* PARL_IO */
        case 0x0C4: /* SPI2 */
        case 0x0CC: /* AES */
        case 0x0D0: /* SHA */
        case 0x0D4: /* RSA */
        case 0x0DC: /* ECC */
        case 0x0E4: /* DS */
        case 0x0E8: /* HMAC */
        case 0x0EC: /* ECDSA */
        case 0x164: /* KM */
        case 0x16C: /* TCM_MEM_MONITOR */
        case 0x170: /* PSRAM_MEM_MONITOR */
            return (1 << 2);
        case 0x018: /* MSPI */
            return (1 << 3);
        case 0x054: /* TIMERGROUP0 */
        case 0x060: /* TIMERGROUP1 */
            return (1 << 2) | (1 << 3) | (1 << 4);
        case 0x074: /* I2S */
            return (1 << 2) | (1 << 3);
        default:
            return 0;
    }
}

/* Report the clock selected by the guest, which sets the CPU cycle counter rate. */
static void esp32c5_pcr_update_cpu_freq(ESP32C5PcrState *s)
{
    static const uint32_t soc_clk_hz[] = {
        48000000, ESP_RC_FAST_FREQ, 160000000, 240000000,
    };
    const uint32_t sel = extract32(s->reg[PCR_SYSCLK_CONF_REG / 4], 16, 2);
    const uint32_t div = extract32(s->reg[PCR_CPU_FREQ_CONF_REG / 4], 0, 8) + 1;

    if (s->cpu_freq_changed) {
        s->cpu_freq_changed(s->cpu_freq_opaque, soc_clk_hz[sel] / div);
    }
}

static uint64_t esp32c5_pcr_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5PcrState *s = ESP32C5_PCR(opaque);

    switch (addr) {
        case A_SYSCON_RND_DATA_REG:
            {
                static bool init;
                if (!init) {
                    srand(time(NULL));
                    init = true;
                }
                return rand();
            }
        case A_SYSCON_ORIGIN_REG:
            return 0x51454d55;
        default:
            break;
    }

    if (addr < ESP32C5_PCR_IO_SIZE) {
        uint32_t val = s->reg[addr / 4];
        const uint32_t ready = esp32c5_pcr_ready_mask(addr);

        val &= ~ready;
        if (!(val & PCR_RST_EN)) {
            val |= ready;
        }
        return val;
    }

    return 0;
}

static void esp32c5_pcr_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    ESP32C5PcrState *s = ESP32C5_PCR(opaque);

    if (addr < ESP32C5_PCR_IO_SIZE) {
        s->reg[addr / 4] = (uint32_t)value;

        if (addr == PCR_BUS_CLK_UPDATE_REG) {
            s->reg[addr / 4] &= ~1;
        } else if (addr == PCR_SYSCLK_CONF_REG) {
            s->reg[addr / 4] &= ~PCR_SYSCLK_XTAL_FREQ_MASK;
            s->reg[addr / 4] |= PCR_SYSCLK_XTAL_FREQ_MHZ;
            esp32c5_pcr_update_cpu_freq(s);
        } else if (addr == PCR_CPU_FREQ_CONF_REG) {
            esp32c5_pcr_update_cpu_freq(s);
        }
    }
}

static const MemoryRegionOps esp32c5_pcr_ops = {
    .read  = esp32c5_pcr_read,
    .write = esp32c5_pcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c5_pcr_init(Object *obj)
{
    ESP32C5PcrState *s = ESP32C5_PCR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c5_pcr_ops, s,
                          TYPE_ESP32C5_PCR, ESP32C5_PCR_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);

}

static void esp32c5_pcr_reset_hold(Object *obj, ResetType type)
{
    ESP32C5PcrState *s = ESP32C5_PCR(obj);

    s->reg[PCR_SYSCLK_CONF_REG / 4] = PCR_SYSCLK_XTAL_FREQ_MHZ | PCR_SYSCLK_HS_DIV_NUM_RESET;
    s->reg[PCR_MSPI_CLK_CONF_REG / 4] = PCR_MSPI_FUNC_CLK_EN;
    s->reg[PCR_CPU_FREQ_CONF_REG / 4] = 0;
    esp32c5_pcr_update_cpu_freq(s);
}

static void esp32c5_pcr_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c5_pcr_reset_hold;
}

static const TypeInfo esp32c5_pcr_info = {
    .name = TYPE_ESP32C5_PCR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5PcrState),
    .instance_init = esp32c5_pcr_init,
    .class_init = esp32c5_pcr_class_init,
};

static void esp32c5_pcr_register_types(void)
{
    type_register_static(&esp32c5_pcr_info);
}

type_init(esp32c5_pcr_register_types)

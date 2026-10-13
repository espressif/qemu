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
#include <stdlib.h>
#include <time.h>

#define H4_PCR_SYSCLK_CONF_REG     0x114
#define H4_PCR_BUS_CLK_UPDATE_REG  0x148
#define H4_A_SYSCON_RND_DATA_REG   0x0B0
#define H4_A_SYSCON_ORIGIN_REG     0x3F8

/*
 * Registers that must read back their true silicon value because the guest
 * tests individual bits in them. They are excluded from the generic
 * "reset deasserted implies ready" fudge applied to every other offset.
 */
#define H4_PCR_INTMTX_CONF_REG     0x098
#define H4_PCR_CORE1_CONF_REG      0x188
#define H4_PCR_CORE1_CLK_EN        (1U << 0)
#define H4_PCR_CORE1_RST_EN        (1U << 1)

/* Reset values, from IDF soc/esp32h4/register/soc/pcr_reg.h */
static const struct {
    hwaddr offset;
    uint32_t value;
} pcr_reset[] = {
    /* CORE0 intmtx clocked and ready; CORE1 intmtx held in reset. */
    { H4_PCR_INTMTX_CONF_REG, 0x00000039 },
    /* CORE1_RST_EN comes up asserted and CORE1_CLK_EN stays off until core0
     * releases the core. */
    { H4_PCR_CORE1_CONF_REG,  H4_PCR_CORE1_RST_EN },
};

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
    case H4_PCR_SYSCLK_CONF_REG:
        /* XTAL freq in bits [30:24] — ESP32-H4 is 32 MHz */
        return (32 << 24) | (1 << 16) | (2 << 8);
    case H4_A_SYSCON_ORIGIN_REG:
        return 0x51454d55;
    default:
        break;
    }

    if (addr < ESP32H4_PCR_IO_SIZE) {
        uint32_t val = s->reg[addr / 4];
        if (addr == H4_PCR_CORE1_CONF_REG || addr == H4_PCR_INTMTX_CONF_REG) {
            return val;
        }
        if (!(val & (1 << 1))) {
            val |= (1 << 2) | (1 << 3);
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

/*
 * ESP32-H4 PCR (Peripheral Clock and Reset) controller
 *
 * Standalone: SYSCLK_CONF at 0x114, BUS_CLK_UPDATE at 0x148, XTAL 32 MHz.
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
#include "hw/misc/esp32h4_pcr.h"
#include <stdlib.h>
#include <time.h>

#define H4_PCR_SYSCLK_CONF_REG     0x114
#define H4_PCR_BUS_CLK_UPDATE_REG  0x148
#define H4_A_SYSCON_RND_DATA_REG   0x0B0
#define H4_A_SYSCON_ORIGIN_REG     0x3F8

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
    }
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
}

static const TypeInfo esp32h4_pcr_info = {
    .name = TYPE_ESP32H4_PCR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H4PcrState),
    .instance_init = esp32h4_pcr_init,
};

static void esp32h4_pcr_register_types(void)
{
    type_register_static(&esp32h4_pcr_info);
}
type_init(esp32h4_pcr_register_types)

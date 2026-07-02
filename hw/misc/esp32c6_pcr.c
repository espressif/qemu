/*
 * ESP32-C6 PCR (Peripheral Clock and Reset) controller emulation
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
#include "hw/registerfields.h"
#include "hw/misc/esp32c6_pcr.h"
#include <stdlib.h>
#include <time.h>

/* ======================================================================
 * ESP32-C6 PCR / SYSCON register layout.
 * From ESP-IDF: components/soc/esp32c6/register/soc/pcr_reg.h
 *               components/soc/esp32c6/register/soc/syscon_reg.h
 * ====================================================================== */

REG32(PCR_MSPI_CLK_CONF, 0x01C)
    FIELD(PCR_MSPI_CLK_CONF, FAST_LS_DIV_NUM, 0,  8)
    FIELD(PCR_MSPI_CLK_CONF, FAST_HS_DIV_NUM, 8,  8)
REG32(SYSCON_RND_DATA,   0x0B0)
REG32(PCR_SYSCLK_CONF,   0x110)
    FIELD(PCR_SYSCLK_CONF, HS_DIV_NUM,    8,  8)
    FIELD(PCR_SYSCLK_CONF, SOC_CLK_SEL,   16, 2)
    FIELD(PCR_SYSCLK_CONF, CLK_XTAL_FREQ, 24, 7)
REG32(SYSCON_ORIGIN,     0x3F8) /* QEMU magic identifier "QEMU" */

/* SOC_CLK_SEL values (matching IDF's SOC_CPU_CLK_SRC_*) */
#define PCR_SYSCLK_SEL_PLL  1

/* PCR_MSPI fast-clock configuration: divider 5 (480 MHz PLL / 6 = 80 MHz) */
#define PCR_MSPI_FAST_HS_DIV_DEFAULT 5
#define PCR_MSPI_FAST_LS_DIV_DEFAULT 0

/* Crystal frequency reported to IDF (40 MHz) */
#define PCR_SYSCLK_CRYSTAL_FREQ_MHZ  40
/* Default high-speed divider value */
#define PCR_SYSCLK_HS_DIV_DEFAULT    2

/* "QEMU" magic word in little-endian, returned at SYSCON_ORIGIN */
#define SYSCON_ORIGIN_QEMU_MAGIC     0x51454d55

static uint64_t esp32c6_pcr_read(void *opaque, hwaddr addr, unsigned int size)
{
    switch (addr) {
    case A_PCR_MSPI_CLK_CONF: {
        uint32_t r = 0;
        r = FIELD_DP32(r, PCR_MSPI_CLK_CONF, FAST_HS_DIV_NUM,
                       PCR_MSPI_FAST_HS_DIV_DEFAULT);
        r = FIELD_DP32(r, PCR_MSPI_CLK_CONF, FAST_LS_DIV_NUM,
                       PCR_MSPI_FAST_LS_DIV_DEFAULT);
        return r;
    }

    case A_SYSCON_RND_DATA: {
        static bool init;
        if (!init) {
            srand(time(NULL));
            init = true;
        }
        return rand();
    }

    case A_PCR_SYSCLK_CONF: {
        uint32_t r = 0;
        r = FIELD_DP32(r, PCR_SYSCLK_CONF, CLK_XTAL_FREQ,
                       PCR_SYSCLK_CRYSTAL_FREQ_MHZ);
        r = FIELD_DP32(r, PCR_SYSCLK_CONF, SOC_CLK_SEL, PCR_SYSCLK_SEL_PLL);
        r = FIELD_DP32(r, PCR_SYSCLK_CONF, HS_DIV_NUM,
                       PCR_SYSCLK_HS_DIV_DEFAULT);
        return r;
    }

    case A_SYSCON_ORIGIN:
        return SYSCON_ORIGIN_QEMU_MAGIC;

    default:
        return 0;
    }
}

static void esp32c6_pcr_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
}

static const MemoryRegionOps esp32c6_pcr_ops = {
    .read  = esp32c6_pcr_read,
    .write = esp32c6_pcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_pcr_init(Object *obj)
{
    ESP32C6PcrState *s = ESP32C6_PCR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_pcr_ops, s,
                          TYPE_ESP32C6_PCR, ESP32C6_PCR_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c6_pcr_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c6_pcr_info = {
    .name = TYPE_ESP32C6_PCR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6PcrState),
    .instance_init = esp32c6_pcr_init,
    .class_init = esp32c6_pcr_class_init,
};

static void esp32c6_pcr_register_types(void)
{
    type_register_static(&esp32c6_pcr_info);
}

type_init(esp32c6_pcr_register_types)

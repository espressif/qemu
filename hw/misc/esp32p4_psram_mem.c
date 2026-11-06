/*
 * ESP32-P4 PSRAM MSPI (SPI_MEM_S) controller emulation
 *
 * Provides the 64 MB cached PSRAM window at 0x48000000 and accepts the
 * 1024-entry MMU table programming that NuttX/IDF perform at boot. The
 * window is plain RAM — physical page remapping is treated as identity
 * (entry i always backs vaddr 0x48000000 + i*64KB), which is enough for
 * heap/XIP use. AXI FIFO status bits must read idle or software spins.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32p4_psram_mem.h"

#define A_P4_PSRAM_CMD                  0x000
#define A_P4_PSRAM_SMEM_AXI_ADDR_CTRL   0x178
#define A_P4_PSRAM_TIMING_CALI          0x180
#define A_P4_PSRAM_MMU_ITEM_CONTENT     0x37C
#define A_P4_PSRAM_MMU_ITEM_INDEX       0x380
#define A_P4_PSRAM_MMU_POWER_CTRL       0x384

#define P4_PSRAM_AXI_IDLE_BITS          0xFC000000
#define P4_PSRAM_TIMING_CLK_ENA         (1 << 0)
#define P4_PSRAM_MMU_MEM_FORCE_PU       (1 << 2)

static uint64_t esp32p4_psram_mem_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    ESP32P4PsramMemState *s = ESP32P4_PSRAM_MEM(opaque);

    switch (addr) {
    case A_P4_PSRAM_CMD:
        return 0;

    case A_P4_PSRAM_SMEM_AXI_ADDR_CTRL:
        return P4_PSRAM_AXI_IDLE_BITS;

    case A_P4_PSRAM_TIMING_CALI:
        return P4_PSRAM_TIMING_CLK_ENA;

    case A_P4_PSRAM_MMU_ITEM_CONTENT:
        if (s->mmu_item_index < ESP32P4_MMU_ENTRY_COUNT) {
            return s->mmu_entries[s->mmu_item_index];
        }
        return 0;

    case A_P4_PSRAM_MMU_ITEM_INDEX:
        return s->mmu_item_index;

    case A_P4_PSRAM_MMU_POWER_CTRL:
        return s->mmu_power_ctrl;

    default:
        return 0;
    }
}

static void esp32p4_psram_mem_write(void *opaque, hwaddr addr, uint64_t value,
                                    unsigned int size)
{
    ESP32P4PsramMemState *s = ESP32P4_PSRAM_MEM(opaque);

    switch (addr) {
    case A_P4_PSRAM_MMU_ITEM_INDEX:
        s->mmu_item_index = (uint32_t)value;
        break;

    case A_P4_PSRAM_MMU_ITEM_CONTENT:
        if (s->mmu_item_index < ESP32P4_MMU_ENTRY_COUNT) {
            s->mmu_entries[s->mmu_item_index] = (uint32_t)value;
        }
        break;

    case A_P4_PSRAM_MMU_POWER_CTRL:
        s->mmu_power_ctrl = (uint32_t)value;
        break;

    default:
        break;
    }
}

static const MemoryRegionOps esp32p4_psram_mem_ops = {
    .read = esp32p4_psram_mem_read,
    .write = esp32p4_psram_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32p4_psram_mem_realize(DeviceState *dev, Error **errp)
{
    ESP32P4PsramMemState *s = ESP32P4_PSRAM_MEM(dev);

    memory_region_init_ram(&s->psram, OBJECT(dev), "esp32p4.psram",
                           ESP32P4_PSRAM_WINDOW_SIZE, &error_abort);
}

static void esp32p4_psram_mem_init(Object *obj)
{
    ESP32P4PsramMemState *s = ESP32P4_PSRAM_MEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32p4_psram_mem_ops, s,
                          TYPE_ESP32P4_PSRAM_MEM, ESP32P4_PSRAM_MEM_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    s->mmu_power_ctrl = P4_PSRAM_MMU_MEM_FORCE_PU;
}

static void esp32p4_psram_mem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32p4_psram_mem_realize;
}

static const TypeInfo esp32p4_psram_mem_info = {
    .name = TYPE_ESP32P4_PSRAM_MEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32P4PsramMemState),
    .instance_init = esp32p4_psram_mem_init,
    .class_init = esp32p4_psram_mem_class_init,
};

static void esp32p4_psram_mem_register_types(void)
{
    type_register_static(&esp32p4_psram_mem_info);
}

type_init(esp32p4_psram_mem_register_types)

/*
 * ESP32-C5 SPI_MEM (SPI0) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c5_spi_mem.h"
#include "sysemu/block-backend-io.h"
#include "exec/memory.h"

#define SPI_MEM_DEBUG 0

#define SPI_MEM_CMD_REG            0x000
#define SPI_MEM_FSM_REG            0x054
#define SPI_MEM_TIMING_CALI_REG    0x170
#define SPI_SMEM_AXI_ADDR_CTRL_REG 0x178
#define SPI_MEM_MMU_ITEM_CONTENT   0x37C
#define SPI_MEM_MMU_ITEM_INDEX     0x380
#define SPI_MEM_MMU_POWER_CTRL     0x384

static uint32_t esp32c5_spi_mem_page_size(ESP32C5SpiMemState *s)
{
    uint32_t code = (s->mmu_power_ctrl >> 3) & 3;
    switch (code) {
    case 0: return 64 * 1024;
    case 1: return 32 * 1024;
    case 2: return 16 * 1024;
    case 3: return 8 * 1024;
    default: return 64 * 1024;
    }
}

static void esp32c5_spi_mem_load_page(ESP32C5SpiMemState *s, uint32_t entry_index,
                                      uint32_t mmu_val)
{
    if (!s->flash_blk || !s->dcache_mr) {
        return;
    }
    uint32_t page_size = esp32c5_spi_mem_page_size(s);
    uint32_t virtual_offset = entry_index * page_size;
    uint8_t *cache_data = ((uint8_t *)memory_region_get_ram_ptr(s->dcache_mr))
                         + virtual_offset;
    if (virtual_offset + page_size > memory_region_size(s->dcache_mr)) {
        return;
    }
    if (mmu_val & C5_MMU_VALID_BIT) {
        uint32_t page_number = mmu_val & C5_MMU_PAGE_NUM_MASK;
        uint32_t physical_offset = page_number * page_size;
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: load page entry=%u page=%u flash_off=0x%x virt_off=0x%x page_sz=%u\n",
                 entry_index, page_number, physical_offset, virtual_offset, page_size);
#endif
        blk_pread(s->flash_blk, physical_offset, page_size, cache_data, 0);
#if SPI_MEM_DEBUG
        {
            uint32_t *w = (uint32_t *)cache_data;
            qemu_log("SPI_MEM: verify entry=%u first words: %08x %08x %08x %08x\n",
                     entry_index, w[0], w[1], w[2], w[3]);
            uint32_t *w2 = (uint32_t *)(cache_data + 0x6728);
            if (entry_index == 2 && page_size > 0x6728 + 16) {
                qemu_log("SPI_MEM: entry=2 @0x6728: %08x %08x %08x %08x\n",
                         w2[0], w2[1], w2[2], w2[3]);
            }
        }
#endif
    } else {
        uint32_t *word_data = (uint32_t *)cache_data;
        for (uint32_t i = 0; i < page_size / sizeof(uint32_t); i++) {
            word_data[i] = 0xDEADBEEF;
        }
    }
    memory_region_set_dirty(s->dcache_mr, virtual_offset, page_size);
}

static uint64_t esp32c5_spi_mem_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5SpiMemState *s = ESP32C5_SPI_MEM(opaque);
    uint64_t r = 0;

    switch (addr) {
    case SPI_MEM_CMD_REG:
        r = 0;
        break;
    case SPI_MEM_FSM_REG:
        r = (1 << 1);
        break;
    case SPI_MEM_TIMING_CALI_REG:
        r = (1 << 31);
        break;
    case SPI_SMEM_AXI_ADDR_CTRL_REG:
        r = 0xFC000000;
        break;
    case SPI_MEM_MMU_ITEM_CONTENT:
        if (s->mmu_item_index < C5_MMU_ENTRY_COUNT) {
            r = s->mmu_entries[s->mmu_item_index];
        }
        break;
    case SPI_MEM_MMU_ITEM_INDEX:
        r = s->mmu_item_index;
        break;
    case SPI_MEM_MMU_POWER_CTRL:
        r = s->mmu_power_ctrl;
        break;
    default:
        break;
    }
    return r;
}

static void esp32c5_spi_mem_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned int size)
{
    ESP32C5SpiMemState *s = ESP32C5_SPI_MEM(opaque);

    switch (addr) {
    case SPI_MEM_MMU_ITEM_INDEX:
        s->mmu_item_index = (uint32_t)value;
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: set index=%u\n", s->mmu_item_index);
#endif
        break;
    case SPI_MEM_MMU_ITEM_CONTENT:
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: write content entry=%u val=0x%x\n",
                 s->mmu_item_index, (uint32_t)value);
#endif
        if (s->mmu_item_index < C5_MMU_ENTRY_COUNT) {
            s->mmu_entries[s->mmu_item_index] = (uint32_t)value;
            esp32c5_spi_mem_load_page(s, s->mmu_item_index, (uint32_t)value);
        }
        break;
    case SPI_MEM_MMU_POWER_CTRL:
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: set power_ctrl=0x%x (page_size=%u)\n",
                 (uint32_t)value, ((uint32_t)value >> 3) & 3);
#endif
        s->mmu_power_ctrl = (uint32_t)value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32c5_spi_mem_ops = {
    .read  = esp32c5_spi_mem_read,
    .write = esp32c5_spi_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c5_spi_mem_init(Object *obj)
{
    ESP32C5SpiMemState *s = ESP32C5_SPI_MEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c5_spi_mem_ops, s,
                          TYPE_ESP32C5_SPI_MEM, ESP32C5_SPI_MEM_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);

    s->mmu_power_ctrl = (1 << 2);
}

static void esp32c5_spi_mem_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c5_spi_mem_info = {
    .name = TYPE_ESP32C5_SPI_MEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5SpiMemState),
    .instance_init = esp32c5_spi_mem_init,
    .class_init = esp32c5_spi_mem_class_init,
};

static void esp32c5_spi_mem_register_types(void)
{
    type_register_static(&esp32c5_spi_mem_info);
}

type_init(esp32c5_spi_mem_register_types)

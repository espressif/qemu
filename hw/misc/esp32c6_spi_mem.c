/*
 * ESP32-C6 SPI_MEM (SPI0) controller
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
#include "hw/registerfields.h"
#include "hw/misc/esp32c6_spi_mem.h"
#include "sysemu/block-backend-io.h"
#include "exec/memory.h"

#define SPI_MEM_DEBUG 0

/* ======================================================================
 * ESP32-C6 SPI_MEM (SPI0) register layout.
 * From ESP-IDF: components/soc/esp32c6/register/soc/spi_mem_reg.h.
 * ====================================================================== */

REG32(SPI_MEM_TIMING_CALI,    0x170)
    FIELD(SPI_MEM_TIMING_CALI, CALI_DONE,   31, 1)
REG32(SPI_MEM_MMU_ITEM_CONTENT, 0x37C)
REG32(SPI_MEM_MMU_ITEM_INDEX,   0x380)
REG32(SPI_MEM_MMU_POWER_CTRL,   0x384)
    FIELD(SPI_MEM_MMU_POWER_CTRL, FORCE_PU,  2, 1)
    FIELD(SPI_MEM_MMU_POWER_CTRL, PAGE_SIZE, 3, 2) /* 0=64KB,1=32KB,2=16KB,3=8KB */

static uint32_t esp32c6_spi_mem_page_size(ESP32C6SpiMemState *s)
{
    switch (FIELD_EX32(s->mmu_power_ctrl, SPI_MEM_MMU_POWER_CTRL, PAGE_SIZE)) {
    case 0: return 64 * 1024;
    case 1: return 32 * 1024;
    case 2: return 16 * 1024;
    case 3: return 8  * 1024;
    default: return 64 * 1024;
    }
}

static void esp32c6_spi_mem_load_page(ESP32C6SpiMemState *s, uint32_t entry_index,
                                      uint32_t mmu_val)
{
    if (!s->flash_blk || !s->dcache_mr) {
        return;
    }
    uint32_t page_size = esp32c6_spi_mem_page_size(s);
    uint32_t virtual_offset = entry_index * page_size;
    uint8_t *cache_data = ((uint8_t *)memory_region_get_ram_ptr(s->dcache_mr))
                         + virtual_offset;
    if (virtual_offset + page_size > memory_region_size(s->dcache_mr)) {
        return;
    }
    if (mmu_val & C6_MMU_VALID_BIT) {
        uint32_t page_number = mmu_val & C6_MMU_PAGE_NUM_MASK;
        uint32_t physical_offset = page_number * page_size;
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: load page entry=%u page=%u flash_off=0x%x virt_off=0x%x page_sz=%u\n",
                 entry_index, page_number, physical_offset, virtual_offset, page_size);
#endif
        blk_pread(s->flash_blk, physical_offset, page_size, cache_data, 0);
    } else {
        uint32_t *word_data = (uint32_t *)cache_data;
        for (uint32_t i = 0; i < page_size / sizeof(uint32_t); i++) {
            word_data[i] = 0xDEADBEEF;
        }
    }
    memory_region_set_dirty(s->dcache_mr, virtual_offset, page_size);
}

static uint64_t esp32c6_spi_mem_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C6SpiMemState *s = ESP32C6_SPI_MEM(opaque);

    switch (addr) {
    case A_SPI_MEM_TIMING_CALI:
        /* IDF/ROM polls CALI_DONE; we don't model the calibration so report
         * it as immediately complete. */
        return R_SPI_MEM_TIMING_CALI_CALI_DONE_MASK;

    case A_SPI_MEM_MMU_ITEM_CONTENT:
        if (s->mmu_item_index < C6_MMU_ENTRY_COUNT) {
            return s->mmu_entries[s->mmu_item_index];
        }
        return 0;

    case A_SPI_MEM_MMU_ITEM_INDEX:
        return s->mmu_item_index;

    case A_SPI_MEM_MMU_POWER_CTRL:
        return s->mmu_power_ctrl;

    default:
        return 0;
    }
}

static void esp32c6_spi_mem_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned int size)
{
    ESP32C6SpiMemState *s = ESP32C6_SPI_MEM(opaque);

    switch (addr) {
    case A_SPI_MEM_MMU_ITEM_INDEX:
        s->mmu_item_index = (uint32_t)value;
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: set index=%u\n", s->mmu_item_index);
#endif
        break;

    case A_SPI_MEM_MMU_ITEM_CONTENT:
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: write content entry=%u val=0x%x\n",
                 s->mmu_item_index, (uint32_t)value);
#endif
        if (s->mmu_item_index < C6_MMU_ENTRY_COUNT) {
            s->mmu_entries[s->mmu_item_index] = (uint32_t)value;
            esp32c6_spi_mem_load_page(s, s->mmu_item_index, (uint32_t)value);
        }
        break;

    case A_SPI_MEM_MMU_POWER_CTRL:
#if SPI_MEM_DEBUG
        qemu_log("SPI_MEM: set power_ctrl=0x%x (page_size=%u)\n",
                 (uint32_t)value,
                 (uint32_t)FIELD_EX32(value, SPI_MEM_MMU_POWER_CTRL, PAGE_SIZE));
#endif
        s->mmu_power_ctrl = (uint32_t)value;
        break;

    default:
        break;
    }
}

static const MemoryRegionOps esp32c6_spi_mem_ops = {
    .read  = esp32c6_spi_mem_read,
    .write = esp32c6_spi_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_spi_mem_init(Object *obj)
{
    ESP32C6SpiMemState *s = ESP32C6_SPI_MEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_spi_mem_ops, s,
                          TYPE_ESP32C6_SPI_MEM, ESP32C6_SPI_MEM_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);

    /* Default: 64 KB pages (PAGE_SIZE field = 0), MMU memory force-powered-up */
    s->mmu_power_ctrl = R_SPI_MEM_MMU_POWER_CTRL_FORCE_PU_MASK;
}

static void esp32c6_spi_mem_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c6_spi_mem_info = {
    .name = TYPE_ESP32C6_SPI_MEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6SpiMemState),
    .instance_init = esp32c6_spi_mem_init,
    .class_init = esp32c6_spi_mem_class_init,
};

static void esp32c6_spi_mem_register_types(void)
{
    type_register_static(&esp32c6_spi_mem_info);
}

type_init(esp32c6_spi_mem_register_types)

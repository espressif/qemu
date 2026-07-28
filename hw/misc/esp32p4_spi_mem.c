/*
 * ESP32-P4 SPI_MEM (SPI0 / flash MSPI) controller emulation
 *
 * Only what the ROM and IDF startup need: the AXI FIFO status bits must
 * read back as "idle" (they are RO-1 out of reset) and MMU entry writes
 * pull the corresponding flash page into the cached flash window.
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
#include "hw/misc/esp32p4_spi_mem.h"
#include "sysemu/block-backend-io.h"
#include "exec/memory.h"

#define A_P4_SPI_MEM_CMD                0x000
#define A_P4_SPI_MEM_SMEM_AXI_ADDR_CTRL 0x178
#define A_P4_SPI_MEM_TIMING_CALI        0x180
#define A_P4_SPI_MEM_MMU_ITEM_CONTENT   0x37C
#define A_P4_SPI_MEM_MMU_ITEM_INDEX     0x380
#define A_P4_SPI_MEM_MMU_POWER_CTRL     0x384

/* ALL_FIFO_EMPTY and the four AFIFO *_REMPTY bits, all RO with reset value 1 */
#define P4_SPI_MEM_AXI_IDLE_BITS        0xFC000000

#define P4_SPI_MEM_TIMING_CLK_ENA       (1 << 0)
#define P4_SPI_MEM_MMU_MEM_FORCE_PU     (1 << 2)

static void esp32p4_spi_mem_load_page(ESP32P4SpiMemState *s,
                                      uint32_t entry_index, uint32_t mmu_val)
{
    if (!s->flash_blk || !s->flash_win_mr) {
        return;
    }

    const uint32_t page_size = ESP32P4_MMU_PAGE_SIZE;
    const uint32_t virtual_offset = entry_index * page_size;

    if (virtual_offset + page_size > memory_region_size(s->flash_win_mr)) {
        return;
    }

    uint8_t *window = (uint8_t *)memory_region_get_ram_ptr(s->flash_win_mr)
                    + virtual_offset;

    if (mmu_val & P4_MMU_VALID_BIT) {
        uint32_t physical_offset = (mmu_val & P4_MMU_PAGE_NUM_MASK) * page_size;
        blk_pread(s->flash_blk, physical_offset, page_size, window, 0);
    } else {
        uint32_t *words = (uint32_t *)window;
        for (uint32_t i = 0; i < page_size / sizeof(uint32_t); i++) {
            words[i] = 0xDEADBEEF;
        }
    }

    memory_region_set_dirty(s->flash_win_mr, virtual_offset, page_size);
}

static uint64_t esp32p4_spi_mem_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    ESP32P4SpiMemState *s = ESP32P4_SPI_MEM(opaque);

    switch (addr) {
    case A_P4_SPI_MEM_CMD:
        /* No transfer ever pending */
        return 0;

    case A_P4_SPI_MEM_SMEM_AXI_ADDR_CTRL:
        return P4_SPI_MEM_AXI_IDLE_BITS;

    case A_P4_SPI_MEM_TIMING_CALI:
        return P4_SPI_MEM_TIMING_CLK_ENA;

    case A_P4_SPI_MEM_MMU_ITEM_CONTENT:
        if (s->mmu_item_index < ESP32P4_MMU_ENTRY_COUNT) {
            return s->mmu_entries[s->mmu_item_index];
        }
        return 0;

    case A_P4_SPI_MEM_MMU_ITEM_INDEX:
        return s->mmu_item_index;

    case A_P4_SPI_MEM_MMU_POWER_CTRL:
        return s->mmu_power_ctrl;

    default:
        return 0;
    }
}

static void esp32p4_spi_mem_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned int size)
{
    ESP32P4SpiMemState *s = ESP32P4_SPI_MEM(opaque);

    switch (addr) {
    case A_P4_SPI_MEM_MMU_ITEM_INDEX:
        s->mmu_item_index = (uint32_t)value;
        break;

    case A_P4_SPI_MEM_MMU_ITEM_CONTENT:
        if (s->mmu_item_index < ESP32P4_MMU_ENTRY_COUNT) {
            s->mmu_entries[s->mmu_item_index] = (uint32_t)value;
            esp32p4_spi_mem_load_page(s, s->mmu_item_index, (uint32_t)value);
        }
        break;

    case A_P4_SPI_MEM_MMU_POWER_CTRL:
        s->mmu_power_ctrl = (uint32_t)value;
        break;

    default:
        break;
    }
}

static const MemoryRegionOps esp32p4_spi_mem_ops = {
    .read = esp32p4_spi_mem_read,
    .write = esp32p4_spi_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32p4_spi_mem_init(Object *obj)
{
    ESP32P4SpiMemState *s = ESP32P4_SPI_MEM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32p4_spi_mem_ops, s,
                          TYPE_ESP32P4_SPI_MEM, ESP32P4_SPI_MEM_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    s->mmu_power_ctrl = P4_SPI_MEM_MMU_MEM_FORCE_PU;
}

static const TypeInfo esp32p4_spi_mem_info = {
    .name = TYPE_ESP32P4_SPI_MEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32P4SpiMemState),
    .instance_init = esp32p4_spi_mem_init,
};

static void esp32p4_spi_mem_register_types(void)
{
    type_register_static(&esp32p4_spi_mem_info);
}

type_init(esp32p4_spi_mem_register_types)

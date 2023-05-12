/*
 * ESP32-C3 ICache emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */


#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/esp32c3_cache.h"
#include "sysemu/block-backend-io.h"


#define CACHE_DEBUG      0
#define CACHE_WARNING    0


/**
 * @brief Checks that the enable flag is enabled in the I/O register. If that's the case,
 *        `done` flag is returned and the `enable` flag is cleared from register.
 *        Else, 0 is returned.
*/
static inline uint32_t check_and_reset_ena(uint32_t* hwreg, uint32_t ena_mask, uint32_t done_mask)
{
    uint32_t regval = *hwreg;

    if (regval & ena_mask) {
        regval &= ~ena_mask;
        regval |= done_mask;
        *hwreg = regval;
    }

    return regval;
}


static inline uint32_t esp32c3_read_mmu_value(ESP32C3CacheState *s, hwaddr reg_addr)
{
    /* Make the assumption that the address is aligned on sizeof(uint32_t) */
    const uint32_t index = reg_addr / sizeof(uint32_t);
    return (uint32_t) s->mmu[index].val;
}


static inline void esp32c3_write_mmu_value(ESP32C3CacheState *s, hwaddr reg_addr, uint32_t value)
{
    /* Make the assumption that the address is aligned on sizeof(uint32_t) */
    const uint32_t index = reg_addr / sizeof(uint32_t);
    /* Reserved bits shall always be 0 */
    ESP32C3MMUEntry e = { .val = value };
    /* Always keep reserved as 0 */
    e.reserved = 0;
    if (s->mmu[index].val != e.val) {
        /* Update the cache (MemoryRegion) */
        const uint32_t virtual_address = index * ESP32C3_PAGE_SIZE;
        /* The entry contains the index of the 64KB block from the flash memory */
        const uint32_t physical_address = e.page_number * ESP32C3_PAGE_SIZE;
        uint8_t* cache_data = ((uint8_t*) memory_region_get_ram_ptr(&s->dcache)) + virtual_address;

        if (e.invalid) {
            const uint32_t invalid_value = 0xdeadbeef;
            uint32_t* cache_word_data = (uint32_t*) cache_data;
            for (int i = 0; i < ESP32C3_PAGE_SIZE / sizeof(invalid_value); i++) {
                cache_word_data[i] = invalid_value;
            }
        } else {
            if (s->flash_blk != NULL) {
                blk_pread(s->flash_blk, physical_address, ESP32C3_PAGE_SIZE, cache_data, 0);
            }
        }
        s->mmu[index].val = e.val;
    }
}


static uint64_t esp32c3_cache_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3CacheState *s = ESP32C3_CACHE(opaque);
    const hwaddr index = ESP32C3_CACHE_REG_IDX(addr);
    uint64_t r = 0;

    if (addr & 0x3) {
        /* Unaligned access, should we fail? */
        error_report("[QEMU] unaligned access to the cache registers");
    }

    switch(addr) {
        case A_EXTMEM_ICACHE_CTRL:
            r = s->icache_enable;
            break;
        /* For the following registers, mark the bit as done only if the feature was enabled */
        case A_EXTMEM_ICACHE_SYNC_CTRL:
            r = check_and_reset_ena(&s->regs[index],
                                    R_EXTMEM_ICACHE_SYNC_CTRL_INVALIDATE_ENA_MASK,
                                    R_EXTMEM_ICACHE_SYNC_CTRL_SYNC_DONE_MASK);
            break;
        case A_EXTMEM_ICACHE_AUTOLOAD_CTRL:
            r = check_and_reset_ena(&s->regs[index],
                                    R_EXTMEM_ICACHE_AUTOLOAD_CTRL_AUTOLOAD_ENA_MASK,
                                    R_EXTMEM_ICACHE_AUTOLOAD_CTRL_AUTOLOAD_DONE_MASK);
            break;
        case A_EXTMEM_ICACHE_PRELOAD_CTRL:
            r = check_and_reset_ena(&s->regs[index],
                                    R_EXTMEM_ICACHE_PRELOAD_CTRL_PRELOAD_ENA_MASK,
                                    R_EXTMEM_ICACHE_PRELOAD_CTRL_PRELOAD_DONE_MASK);
            break;
        case A_EXTMEM_ICACHE_FREEZE:
            r = s->regs[index];
            break;
        case A_EXTMEM_CACHE_STATE:
            /* Return the state of ICache as idle:
             * 1: Idle
             * 0: Busy/Not idle */
            r = 1 << R_EXTMEM_CACHE_STATE_ICACHE_STATE_SHIFT;
            break;
        case ESP32C3_MMU_TABLE_OFFSET ... (ESP32C3_MMU_TABLE_OFFSET + ESP32C3_MMU_SIZE):
            r = esp32c3_read_mmu_value(s, addr - ESP32C3_MMU_TABLE_OFFSET);
            break;
        default:
#if CACHE_WARNING
            warn_report("[CACHE] Unsupported read to 0x%lx", addr);
#endif
            break;
    }

#if CACHE_DEBUG
    info_report("[CACHE] Reading 0x%lx (0x%lx)", addr, r);
#endif

    return r;
}

static void esp32c3_cache_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32C3CacheState *s = ESP32C3_CACHE(opaque);

    const hwaddr index = ESP32C3_CACHE_REG_IDX(addr);

    if (index < ESP32C3_CACHE_REG_COUNT) {
        switch (addr) {
            case A_EXTMEM_ICACHE_CTRL:
                s->icache_enable = value & 1;
                break;
            case A_EXTMEM_ICACHE_FREEZE:
                if (value & R_EXTMEM_ICACHE_FREEZE_ICACHE_FREEZE_ENA_MASK) {
                    /* Enable freeze, set DONE bit */
                    s->regs[index] |= R_EXTMEM_ICACHE_FREEZE_ICACHE_FREEZE_DONE_MASK;
                } else {
                    /* Disable freeze, clear DONE bit */
                    s->regs[index] &= ~R_EXTMEM_ICACHE_FREEZE_ICACHE_FREEZE_DONE_MASK;
                }
                break;
            default:
                s->regs[index] = value;
                break;
        }
    } else if (addr >= ESP32C3_MMU_TABLE_OFFSET) {
        esp32c3_write_mmu_value(s, addr - ESP32C3_MMU_TABLE_OFFSET, value);
    }

#if CACHE_DEBUG
    info_report("[CACHE] Writing 0x%lx = %08lx", addr, value);
#endif

}

static const MemoryRegionOps esp32c3_cache_ops = {
    .read =  esp32c3_cache_read,
    .write = esp32c3_cache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static bool esp32c3_cache_mem_accepts(void *opaque, hwaddr addr,
                                      unsigned size, bool is_write,
                                      MemTxAttrs attrs)
{
    /* Only accept operation in the cache if there are in READ access.
     * TODO: Refuse any access to the cache if disable and trigger an exception. */
    return !is_write;
}

static const MemoryRegionOps esp32c3_cache_mem_ops = {
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.accepts = esp32c3_cache_mem_accepts,
};

static void esp32c3_cache_reset(DeviceState *dev)
{
    ESP32C3CacheState *s = ESP32C3_CACHE(dev);
    memset(s->regs, 0, ESP32C3_CACHE_REG_COUNT * sizeof(*s->regs));

    /* Initialize the MMU with invalid entries */
    for (int i = 0; i < ESP32C3_MMU_TABLE_ENTRY_COUNT; i++) {
        s->mmu[i].invalid = 1;
    }

    /* On reset, autoload must be set to done (ready) */
    s->regs[ESP32C3_CACHE_REG_IDX(A_EXTMEM_ICACHE_AUTOLOAD_CTRL)] = R_EXTMEM_ICACHE_AUTOLOAD_CTRL_AUTOLOAD_DONE_MASK;
    /* Same goes for the manual preload */
    s->regs[ESP32C3_CACHE_REG_IDX(A_EXTMEM_ICACHE_PRELOAD_CTRL)] = R_EXTMEM_ICACHE_PRELOAD_CTRL_PRELOAD_DONE_MASK;
}

static void esp32c3_cache_realize(DeviceState *dev, Error **errp)
{
    /* Initialize the registers */
    esp32c3_cache_reset(dev);
}

static void esp32c3_cache_init(Object *obj)
{
    ESP32C3CacheState *s = ESP32C3_CACHE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Since the cache I/O region and the MMU I/O region are adjacent, let's use the same MemoryRegion object
     * for both, this will simplify the machine architecture. */
    memory_region_init_io(&s->iomem, obj, &esp32c3_cache_ops, s,
                          TYPE_ESP32C3_CACHE, TYPE_ESP32C3_CACHE_IO_SIZE + ESP32C3_MMU_SIZE);

    /* Initialize the data cache area first */
    s->dcache_base = ESP32C3_DCACHE_BASE;
    memory_region_init_rom_device(&s->dcache, OBJECT(s),
                                  &esp32c3_cache_mem_ops, s,
                                  "cpu0-dcache", ESP32C3_EXTMEM_REGION_SIZE, &error_abort);

    /* Same goes for the instruction cache */
    s->icache_base = ESP32C3_ICACHE_BASE;
    memory_region_init_alias(&s->icache, OBJECT(s), "cpu0-icache", &s->dcache, 0, ESP32C3_EXTMEM_REGION_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static Property esp32c3_cache_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c3_cache_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_cache_reset;
    dc->realize = esp32c3_cache_realize;
    device_class_set_props(dc, esp32c3_cache_properties);
}

static const TypeInfo esp32c3_cache_info = {
    .name = TYPE_ESP32C3_CACHE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3CacheState),
    .instance_init = esp32c3_cache_init,
    .class_init = esp32c3_cache_class_init
};

static void esp32c3_cache_register_types(void)
{
    type_register_static(&esp32c3_cache_info);
}

type_init(esp32c3_cache_register_types)

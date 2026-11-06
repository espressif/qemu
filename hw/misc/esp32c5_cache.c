/*
 * ESP32-C5 Cache (EXTMEM) controller emulation
 *
 * Unlike C6, the C5 has a shared D/I cache at 0x42000000 — only one
 * cache memory region (dcache) is created.
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
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/esp32c5_cache.h"


#define CACHE_DEBUG      0
#define CACHE_WARNING    0


static uint64_t esp32c5_cache_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5CacheState *s = ESP32C5_CACHE(opaque);
    const hwaddr index = ESP32C5_CACHE_REG_IDX(addr);
    uint64_t r = 0;

    if (addr & 0x3) {
        error_report("[QEMU] unaligned access to the C5 cache registers");
    }

    switch (addr) {
        case A_EXTMEM_C5_L1_CACHE_CTRL:
            r = s->icache_enable;
            break;

        case A_EXTMEM_C5_L1_CACHE_FREEZE_CTRL:
            r = s->regs[index];
            break;

        case A_EXTMEM_C5_L1_CACHE_SYNC_CTRL:
            r = C5_L1_CACHE_SYNC_DONE_BIT;
            break;

        case A_EXTMEM_C5_L1_CACHE_PRELOAD_CTRL:
            r = C5_L1_CACHE_PRELOAD_DONE_BIT;
            break;

        case A_EXTMEM_C5_L1_CACHE_AUTOLOAD_CTRL:
            r = C5_L1_CACHE_AUTOLOAD_DONE_BIT;
            break;

        default:
#if CACHE_WARNING
            warn_report("[C5-CACHE] Unsupported read to 0x%lx", addr);
#endif
            break;
    }

#if CACHE_DEBUG
    info_report("[C5-CACHE] Reading 0x%lx (0x%lx)", addr, r);
#endif

    return r;
}


static void esp32c5_cache_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32C5CacheState *s = ESP32C5_CACHE(opaque);
    const hwaddr index = ESP32C5_CACHE_REG_IDX(addr);

    if (index < ESP32C5_CACHE_REG_COUNT) {
        switch (addr) {
            case A_EXTMEM_C5_L1_CACHE_CTRL:
                s->icache_enable = value & 1;
                break;

            case A_EXTMEM_C5_L1_CACHE_FREEZE_CTRL:
                s->regs[index] = value;
                if (value & C5_L1_CACHE_FREEZE_EN_BIT) {
                    s->regs[index] |= C5_L1_CACHE_FREEZE_DONE_BIT;
                } else {
                    s->regs[index] &= ~C5_L1_CACHE_FREEZE_DONE_BIT;
                }
                break;

            default:
                s->regs[index] = value;
                break;
        }
    }

#if CACHE_DEBUG
    info_report("[C5-CACHE] Writing 0x%lx = %08lx", addr, value);
#endif
}


static const MemoryRegionOps esp32c5_cache_ops = {
    .read  = esp32c5_cache_read,
    .write = esp32c5_cache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


/* No cache_mem_ops needed — dcache is plain RAM for emulation. */


static void esp32c5_cache_reset_hold(Object *obj, ResetType type)
{
    ESP32C5CacheState *s = ESP32C5_CACHE(obj);
    memset(s->regs, 0, sizeof(s->regs));
}


static void esp32c5_cache_realize(DeviceState *dev, Error **errp)
{
    ESP32C5CacheState *s = ESP32C5_CACHE(dev);

    if (s->dcache_base == 0) {
        s->dcache_base = ESP32C5_DCACHE_BASE;
    }
    memory_region_init_ram(&s->dcache, OBJECT(s),
                           "cpu0-dcache", ESP32C5_EXTMEM_REGION_SIZE,
                           &error_abort);

    esp32c5_cache_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
}


static void esp32c5_cache_init(Object *obj)
{
    ESP32C5CacheState *s = ESP32C5_CACHE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c5_cache_ops, s,
                          TYPE_ESP32C5_CACHE, ESP32C5_CACHE_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}


static Property esp32c5_cache_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c5_cache_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c5_cache_reset_hold;
    dc->realize = esp32c5_cache_realize;
    device_class_set_props(dc, esp32c5_cache_properties);
}

static const TypeInfo esp32c5_cache_info = {
    .name = TYPE_ESP32C5_CACHE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5CacheState),
    .instance_init = esp32c5_cache_init,
    .class_init = esp32c5_cache_class_init,
};

static void esp32c5_cache_register_types(void)
{
    type_register_static(&esp32c5_cache_info);
}

type_init(esp32c5_cache_register_types)

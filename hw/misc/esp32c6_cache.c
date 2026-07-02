/*
 * ESP32-C6 Cache (EXTMEM) controller emulation
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
#include "hw/misc/esp32c6_cache.h"


#define CACHE_DEBUG      0
#define CACHE_WARNING    0


static uint64_t esp32c6_cache_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C6CacheState *s = ESP32C6_CACHE(opaque);
    const hwaddr index = ESP32C6_CACHE_REG_IDX(addr);
    uint64_t r = 0;

    if (addr & 0x3) {
        error_report("[QEMU] unaligned access to the C6 cache registers");
    }

    switch (addr) {
        case A_EXTMEM_C6_L1_CACHE_CTRL:
            r = s->icache_enable;
            break;

        case A_EXTMEM_C6_L1_CACHE_FREEZE_CTRL:
            /* Return stored value which tracks FREEZE_EN -> FREEZE_DONE */
            r = s->regs[index];
            break;

        case A_EXTMEM_C6_L1_CACHE_SYNC_CTRL:
            /* Report SYNC operation as completed immediately */
            r = R_EXTMEM_C6_L1_CACHE_SYNC_CTRL_DONE_MASK;
            break;

        case A_EXTMEM_C6_L1_CACHE_PRELOAD_CTRL:
            /* Report PRELOAD operation as completed immediately */
            r = R_EXTMEM_C6_L1_CACHE_PRELOAD_CTRL_DONE_MASK;
            break;

        case A_EXTMEM_C6_L1_CACHE_AUTOLOAD_CTRL:
            /* Report AUTOLOAD operation as completed immediately */
            r = R_EXTMEM_C6_L1_CACHE_AUTOLOAD_CTRL_DONE_MASK;
            break;

        default:
#if CACHE_WARNING
            warn_report("[C6-CACHE] Unsupported read to 0x%lx", addr);
#endif
            break;
    }

#if CACHE_DEBUG
    info_report("[C6-CACHE] Reading 0x%lx (0x%lx)", addr, r);
#endif

    return r;
}


static void esp32c6_cache_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32C6CacheState *s = ESP32C6_CACHE(opaque);
    const hwaddr index = ESP32C6_CACHE_REG_IDX(addr);

    if (index < ESP32C6_CACHE_REG_COUNT) {
        switch (addr) {
            case A_EXTMEM_C6_L1_CACHE_CTRL:
                s->icache_enable = value & 1;
                break;

            case A_EXTMEM_C6_L1_CACHE_FREEZE_CTRL:
                s->regs[index] = value;
                if (FIELD_EX32(value, EXTMEM_C6_L1_CACHE_FREEZE_CTRL, EN)) {
                    s->regs[index] = FIELD_DP32(s->regs[index],
                                                EXTMEM_C6_L1_CACHE_FREEZE_CTRL,
                                                DONE, 1);
                } else {
                    s->regs[index] = FIELD_DP32(s->regs[index],
                                                EXTMEM_C6_L1_CACHE_FREEZE_CTRL,
                                                DONE, 0);
                }
                break;

            default:
                s->regs[index] = value;
                break;
        }
    }

#if CACHE_DEBUG
    info_report("[C6-CACHE] Writing 0x%lx = %08lx", addr, value);
#endif
}


static const MemoryRegionOps esp32c6_cache_ops = {
    .read  = esp32c6_cache_read,
    .write = esp32c6_cache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static bool esp32c6_cache_mem_accepts(void *opaque, hwaddr addr,
                                      unsigned size, bool is_write,
                                      MemTxAttrs attrs)
{
    /* Only accept READ access to the cache memory regions */
    return !is_write;
}

static const MemoryRegionOps esp32c6_cache_mem_ops = {
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.accepts = esp32c6_cache_mem_accepts,
};


static void esp32c6_cache_reset_hold(Object *obj, ResetType type)
{
    ESP32C6CacheState *s = ESP32C6_CACHE(obj);
    memset(s->regs, 0, sizeof(s->regs));
}


static void esp32c6_cache_realize(DeviceState *dev, Error **errp)
{
    ESP32C6CacheState *s = ESP32C6_CACHE(dev);

    /* Initialize the unified cache area (data and instruction share the same
     * region on ESP32-C6) */
    if (s->cache_base == 0) {
        s->cache_base = ESP32C6_CACHE_BASE;
    }
    memory_region_init_rom_device(&s->cache, OBJECT(s),
                                  &esp32c6_cache_mem_ops, s,
                                  "cpu0-cache", ESP32C6_EXTMEM_REGION_SIZE,
                                  &error_abort);

    /* Initialize registers */
    esp32c6_cache_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
}


static void esp32c6_cache_init(Object *obj)
{
    ESP32C6CacheState *s = ESP32C6_CACHE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_cache_ops, s,
                          TYPE_ESP32C6_CACHE, ESP32C6_CACHE_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}


static Property esp32c6_cache_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c6_cache_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c6_cache_reset_hold;
    dc->realize = esp32c6_cache_realize;
    device_class_set_props(dc, esp32c6_cache_properties);
}

static const TypeInfo esp32c6_cache_info = {
    .name = TYPE_ESP32C6_CACHE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6CacheState),
    .instance_init = esp32c6_cache_init,
    .class_init = esp32c6_cache_class_init,
};

static void esp32c6_cache_register_types(void)
{
    type_register_static(&esp32c6_cache_info);
}

type_init(esp32c6_cache_register_types)

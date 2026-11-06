/*
 * ESP32-H4 Cache (EXTMEM) controller emulation
 *
 * Cache operations complete instantly so ROM/bootloader polling of
 * SYNC/PRELOAD/AUTOLOAD DONE bits does not hang.
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
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32h4_cache.h"

static bool is_preload_ctrl(hwaddr addr)
{
    switch (addr) {
    case A_H4_CACHE_L1_ICACHE0_PRELOAD_CTRL:
    case A_H4_CACHE_L1_ICACHE1_PRELOAD_CTRL:
    case A_H4_CACHE_L1_DCACHE_PRELOAD_CTRL:
        return true;
    default:
        return false;
    }
}

static bool is_autoload_ctrl(hwaddr addr)
{
    switch (addr) {
    case A_H4_CACHE_L1_ICACHE0_AUTOLOAD_CTRL:
    case A_H4_CACHE_L1_ICACHE1_AUTOLOAD_CTRL:
    case A_H4_CACHE_L1_DCACHE_AUTOLOAD_CTRL:
        return true;
    default:
        return false;
    }
}

static uint32_t l1_freeze_value(uint32_t value)
{
    uint32_t out = value;

    if (value & H4_CACHE_L1_ICACHE0_FREEZE_EN_BIT) {
        out |= H4_CACHE_L1_ICACHE0_FREEZE_DONE_BIT;
    } else {
        out &= ~H4_CACHE_L1_ICACHE0_FREEZE_DONE_BIT;
    }
    if (value & H4_CACHE_L1_ICACHE1_FREEZE_EN_BIT) {
        out |= H4_CACHE_L1_ICACHE1_FREEZE_DONE_BIT;
    } else {
        out &= ~H4_CACHE_L1_ICACHE1_FREEZE_DONE_BIT;
    }
    if (value & H4_CACHE_L1_DCACHE_FREEZE_EN_BIT) {
        out |= H4_CACHE_L1_DCACHE_FREEZE_DONE_BIT;
    } else {
        out &= ~H4_CACHE_L1_DCACHE_FREEZE_DONE_BIT;
    }

    return out;
}

static uint64_t esp32h4_cache_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H4CacheState *s = ESP32H4_CACHE(opaque);
    const hwaddr index = ESP32H4_CACHE_REG_IDX(addr);

    if (index >= ESP32H4_CACHE_REG_COUNT) {
        return 0;
    }

    uint32_t r = s->regs[index];

    switch (addr) {
    case A_H4_CACHE_SYNC_CTRL:
        r = (r & ~H4_CACHE_SYNC_ENA_MASK) | H4_CACHE_SYNC_DONE_BIT;
        break;

    case A_H4_CACHE_LOCK_CTRL:
        r = (r & ~H4_CACHE_LOCK_ENA_MASK) | H4_CACHE_LOCK_DONE_BIT;
        break;

    default:
        if (is_preload_ctrl(addr)) {
            r = (r & ~H4_CACHE_PRELOAD_ENA_BIT) | H4_CACHE_PRELOAD_DONE_BIT;
        } else if (is_autoload_ctrl(addr)) {
            /* ENA is a configuration bit and must read back unchanged */
            r |= H4_CACHE_AUTOLOAD_DONE_BIT;
        }
        break;
    }

    return r;
}

static void esp32h4_cache_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32H4CacheState *s = ESP32H4_CACHE(opaque);
    const hwaddr index = ESP32H4_CACHE_REG_IDX(addr);

    if (index >= ESP32H4_CACHE_REG_COUNT) {
        return;
    }

    uint32_t val = (uint32_t)value;

    switch (addr) {
    case A_H4_CACHE_SYNC_CTRL:
        val = (val & ~H4_CACHE_SYNC_ENA_MASK) | H4_CACHE_SYNC_DONE_BIT;
        break;

    case A_H4_CACHE_LOCK_CTRL:
        val = (val & ~H4_CACHE_LOCK_ENA_MASK) | H4_CACHE_LOCK_DONE_BIT;
        break;

    case A_H4_CACHE_L1_FREEZE_CTRL:
        val = l1_freeze_value(val);
        break;

    default:
        if (is_preload_ctrl(addr)) {
            val = (val & ~H4_CACHE_PRELOAD_ENA_BIT) | H4_CACHE_PRELOAD_DONE_BIT;
        } else if (is_autoload_ctrl(addr)) {
            val |= H4_CACHE_AUTOLOAD_DONE_BIT;
        }
        break;
    }

    s->regs[index] = val;
}

static const MemoryRegionOps esp32h4_cache_ops = {
    .read = esp32h4_cache_read,
    .write = esp32h4_cache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32h4_cache_reset_hold(Object *obj, ResetType type)
{
    ESP32H4CacheState *s = ESP32H4_CACHE(obj);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[ESP32H4_CACHE_REG_IDX(A_H4_CACHE_SYNC_CTRL)] =
        H4_CACHE_SYNC_DONE_BIT;
    s->regs[ESP32H4_CACHE_REG_IDX(A_H4_CACHE_LOCK_CTRL)] =
        H4_CACHE_LOCK_DONE_BIT;
    s->regs[ESP32H4_CACHE_REG_IDX(A_H4_CACHE_L1_ICACHE0_AUTOLOAD_CTRL)] =
        H4_CACHE_AUTOLOAD_DONE_BIT;
    s->regs[ESP32H4_CACHE_REG_IDX(A_H4_CACHE_L1_ICACHE1_AUTOLOAD_CTRL)] =
        H4_CACHE_AUTOLOAD_DONE_BIT;
    s->regs[ESP32H4_CACHE_REG_IDX(A_H4_CACHE_L1_DCACHE_AUTOLOAD_CTRL)] =
        H4_CACHE_AUTOLOAD_DONE_BIT;
}

static void esp32h4_cache_realize(DeviceState *dev, Error **errp)
{
    ESP32H4CacheState *s = ESP32H4_CACHE(dev);

    if (s->dcache_base == 0) {
        s->dcache_base = ESP32H4_DCACHE_BASE;
    }
    memory_region_init_ram(&s->dcache, OBJECT(s),
                           "cpu0-dcache", ESP32H4_EXTMEM_REGION_SIZE,
                           &error_abort);

    esp32h4_cache_reset_hold(OBJECT(dev), RESET_TYPE_COLD);
}

static void esp32h4_cache_init(Object *obj)
{
    ESP32H4CacheState *s = ESP32H4_CACHE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32h4_cache_ops, s,
                          TYPE_ESP32H4_CACHE, ESP32H4_CACHE_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32h4_cache_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32h4_cache_reset_hold;
    dc->realize = esp32h4_cache_realize;
}

static const TypeInfo esp32h4_cache_info = {
    .name = TYPE_ESP32H4_CACHE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32H4CacheState),
    .instance_init = esp32h4_cache_init,
    .class_init = esp32h4_cache_class_init,
};

static void esp32h4_cache_register_types(void)
{
    type_register_static(&esp32h4_cache_info);
}

type_init(esp32h4_cache_register_types)

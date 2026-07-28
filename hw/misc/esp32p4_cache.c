/*
 * ESP32-P4 Cache controller emulation
 *
 * Cache operations (sync, lock, preload, autoload, freeze) complete
 * instantly: the self-clearing enable bits are dropped on write and the
 * matching DONE status bit is reported, otherwise the ROM spins forever
 * polling CACHE_SYNC_CTRL_REG during boot.
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
#include "hw/misc/esp32p4_cache.h"

static bool is_preload_ctrl(hwaddr addr)
{
    switch (addr) {
    case A_P4_CACHE_L1_ICACHE0_PRELOAD_CTRL:
    case A_P4_CACHE_L1_ICACHE1_PRELOAD_CTRL:
    case A_P4_CACHE_L1_ICACHE2_PRELOAD_CTRL:
    case A_P4_CACHE_L1_ICACHE3_PRELOAD_CTRL:
    case A_P4_CACHE_L1_DCACHE_PRELOAD_CTRL:
    case A_P4_CACHE_L2_PRELOAD_CTRL:
        return true;
    default:
        return false;
    }
}

static bool is_autoload_ctrl(hwaddr addr)
{
    switch (addr) {
    case A_P4_CACHE_L1_ICACHE0_AUTOLOAD_CTRL:
    case A_P4_CACHE_L1_ICACHE1_AUTOLOAD_CTRL:
    case A_P4_CACHE_L1_ICACHE2_AUTOLOAD_CTRL:
    case A_P4_CACHE_L1_ICACHE3_AUTOLOAD_CTRL:
    case A_P4_CACHE_L1_DCACHE_AUTOLOAD_CTRL:
    case A_P4_CACHE_L2_AUTOLOAD_CTRL:
        return true;
    default:
        return false;
    }
}

/* Mirror each enable bit into its DONE bit so freeze polling terminates */
static uint32_t l1_freeze_value(uint32_t value)
{
    uint32_t out = value;

    for (int unit = 0; unit < P4_CACHE_L1_FREEZE_UNIT_COUNT; unit++) {
        if (value & P4_CACHE_L1_FREEZE_EN(unit)) {
            out |= P4_CACHE_L1_FREEZE_DONE(unit);
        } else {
            out &= ~P4_CACHE_L1_FREEZE_DONE(unit);
        }
    }

    return out;
}

static uint32_t l2_freeze_value(uint32_t value)
{
    if (value & P4_CACHE_L2_FREEZE_EN_BIT) {
        return value | P4_CACHE_L2_FREEZE_DONE_BIT;
    }
    return value & ~P4_CACHE_L2_FREEZE_DONE_BIT;
}

static uint64_t esp32p4_cache_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32P4CacheState *s = ESP32P4_CACHE(opaque);
    const hwaddr index = ESP32P4_CACHE_REG_IDX(addr);

    if (index >= ESP32P4_CACHE_REG_COUNT) {
        return 0;
    }

    uint32_t r = s->regs[index];

    switch (addr) {
    case A_P4_CACHE_SYNC_CTRL:
        r = (r & ~P4_CACHE_SYNC_ENA_MASK) | P4_CACHE_SYNC_DONE_BIT;
        break;

    case A_P4_CACHE_LOCK_CTRL:
        r = (r & ~P4_CACHE_LOCK_ENA_MASK) | P4_CACHE_LOCK_DONE_BIT;
        break;

    default:
        if (is_preload_ctrl(addr)) {
            r = (r & ~P4_CACHE_PRELOAD_ENA_BIT) | P4_CACHE_PRELOAD_DONE_BIT;
        } else if (is_autoload_ctrl(addr)) {
            /* ENA is a configuration bit here and must read back unchanged */
            r |= P4_CACHE_AUTOLOAD_DONE_BIT;
        }
        break;
    }

    return r;
}

static void esp32p4_cache_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32P4CacheState *s = ESP32P4_CACHE(opaque);
    const hwaddr index = ESP32P4_CACHE_REG_IDX(addr);

    if (index >= ESP32P4_CACHE_REG_COUNT) {
        return;
    }

    uint32_t val = (uint32_t)value;

    switch (addr) {
    case A_P4_CACHE_SYNC_CTRL:
        val = (val & ~P4_CACHE_SYNC_ENA_MASK) | P4_CACHE_SYNC_DONE_BIT;
        break;

    case A_P4_CACHE_LOCK_CTRL:
        val = (val & ~P4_CACHE_LOCK_ENA_MASK) | P4_CACHE_LOCK_DONE_BIT;
        break;

    case A_P4_CACHE_L1_FREEZE_CTRL:
        val = l1_freeze_value(val);
        break;

    case A_P4_CACHE_L2_FREEZE_CTRL:
        val = l2_freeze_value(val);
        break;

    default:
        if (is_preload_ctrl(addr)) {
            val = (val & ~P4_CACHE_PRELOAD_ENA_BIT) | P4_CACHE_PRELOAD_DONE_BIT;
        } else if (is_autoload_ctrl(addr)) {
            val |= P4_CACHE_AUTOLOAD_DONE_BIT;
        }
        break;
    }

    s->regs[index] = val;
}

static const MemoryRegionOps esp32p4_cache_ops = {
    .read = esp32p4_cache_read,
    .write = esp32p4_cache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32p4_cache_reset_hold(Object *obj, ResetType type)
{
    ESP32P4CacheState *s = ESP32P4_CACHE(obj);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[ESP32P4_CACHE_REG_IDX(A_P4_CACHE_SYNC_CTRL)] =
        P4_CACHE_SYNC_DONE_BIT;
}

static void esp32p4_cache_init(Object *obj)
{
    ESP32P4CacheState *s = ESP32P4_CACHE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32p4_cache_ops, s,
                          TYPE_ESP32P4_CACHE, ESP32P4_CACHE_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32p4_cache_realize(DeviceState *dev, Error **errp)
{
    ESP32P4CacheState *s = ESP32P4_CACHE(dev);

    memory_region_init_ram(&s->flash_win, OBJECT(dev), "esp32p4.flash_window",
                           ESP32P4_FLASH_WINDOW_SIZE, &error_abort);
}

static void esp32p4_cache_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32p4_cache_reset_hold;
    dc->realize = esp32p4_cache_realize;
}

static const TypeInfo esp32p4_cache_info = {
    .name = TYPE_ESP32P4_CACHE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32P4CacheState),
    .instance_init = esp32p4_cache_init,
    .class_init = esp32p4_cache_class_init,
};

static void esp32p4_cache_register_types(void)
{
    type_register_static(&esp32p4_cache_info);
}

type_init(esp32p4_cache_register_types)

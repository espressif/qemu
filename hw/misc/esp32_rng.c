/*
 * ESP32 Random Number Generator peripheral
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_rng.h"


static uint64_t esp32_rng_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t r = 0;
    qemu_guest_getrandom_nofail(&r, sizeof(r));
    return r;
}

static const MemoryRegionOps esp32_rng_ops = {
    .read =  esp32_rng_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_rng_init(Object *obj)
{
    Esp32RngState *s = ESP32_RNG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_rng_ops, s,
                          TYPE_ESP32_RNG, sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);
}


static const TypeInfo esp32_rng_info = {
    .name = TYPE_ESP32_RNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32RngState),
    .instance_init = esp32_rng_init,
};

static void esp32_rng_register_types(void)
{
    type_register_static(&esp32_rng_info);
}

type_init(esp32_rng_register_types)

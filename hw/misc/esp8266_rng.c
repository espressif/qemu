/*
 * ESP8266 hardware random number generator
 *
 * The device structure follows Espressif's QEMU hw/misc/esp32_rng.c.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"
#include "hw/misc/esp8266_rng.h"

static uint64_t esp8266_rng_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t value;

    qemu_guest_getrandom_nofail(&value, sizeof(value));
    return value;
}

/* WDEV_HWRNG is read-only; without a handler a write would crash QEMU. */
static void esp8266_rng_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
}

static const MemoryRegionOps esp8266_rng_ops = {
    .read = esp8266_rng_read,
    .write = esp8266_rng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void esp8266_rng_init(Object *obj)
{
    Esp8266RngState *s = ESP8266_RNG(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_rng_ops, s,
                          TYPE_ESP8266_RNG, sizeof(uint32_t));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const TypeInfo esp8266_rng_info = {
    .name = TYPE_ESP8266_RNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266RngState),
    .instance_init = esp8266_rng_init,
};

static void esp8266_rng_register_types(void)
{
    type_register_static(&esp8266_rng_info);
}

type_init(esp8266_rng_register_types)

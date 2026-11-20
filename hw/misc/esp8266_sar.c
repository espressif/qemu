/*
 * ESP8266 SAR ADC (TOUT/A0)
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * SAR ADC (TOUT/A0): CTRL at 0x60000d50 (bits 24..26 busy, never set here),
 * eight inverted 11-bit samples at 0x60000d80. The SDK's system_adc_read()
 * corrects each sample's low byte as (lo - 21) * 279 / 256, averages them
 * and scales by 12/11; the samples here are that transfer function inverted,
 * so "adc" is the value analogRead(A0) returns (0..1024).
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/misc/esp8266_sar.h"

static uint32_t esp8266_sar_sample(uint16_t adc)
{
    /*
     * shortcut: eight equal samples, so analogRead() is within 2 LSB of
     * "adc"; dither the samples if a test needs every value exact.
     */
    uint32_t dout = 2 * DIV_ROUND_UP(MIN(adc, ESP8266_SAR_MAX) * 11, 12);
    uint32_t corr = dout & 0xff;
    uint32_t lo = 21;

    while (lo < 0xff && ((lo - 21) * 279) >> 8 < corr) {
        lo++;
    }
    return ~((dout & 0x700) | lo) & 0x7ff;
}

static uint64_t esp8266_sar_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp8266SarState *s = opaque;

    return addr >= 0x30 ? esp8266_sar_sample(s->adc) : 0;
}

static void esp8266_sar_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
}

static const MemoryRegionOps esp8266_sar_ops = {
    .read = esp8266_sar_read,
    .write = esp8266_sar_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void esp8266_sar_init(Object *obj)
{
    Esp8266SarState *s = ESP8266_SAR(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_sar_ops, s,
                          TYPE_ESP8266_SAR, 0x50);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    /* The input voltage, settable at run time (qom-set); not reset. */
    object_property_add_uint16_ptr(obj, "adc", &s->adc,
                                   OBJ_PROP_FLAG_READWRITE);
}

static const VMStateDescription vmstate_esp8266_sar = {
    .name = TYPE_ESP8266_SAR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(adc, Esp8266SarState),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_sar_class_init(ObjectClass *klass, void *data)
{
    DEVICE_CLASS(klass)->vmsd = &vmstate_esp8266_sar;
}

static const TypeInfo esp8266_sar_info = {
    .name = TYPE_ESP8266_SAR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266SarState),
    .instance_init = esp8266_sar_init,
    .class_init = esp8266_sar_class_init,
};

static void esp8266_sar_register_types(void)
{
    type_register_static(&esp8266_sar_info);
}

type_init(esp8266_sar_register_types)

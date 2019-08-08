/*
 * ESP32 RTC_CNTL (RTC block controller) device
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_reg.h"
#include "hw/misc/esp32_rtc_cntl.h"


static uint64_t esp32_rtc_cntl_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32RtcCntlState *s = ESP32_RTC_CNTL(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_RTC_CNTL_STORE0:
    case A_RTC_CNTL_STORE1:
    case A_RTC_CNTL_STORE2:
    case A_RTC_CNTL_STORE3:
        r = s->scratch_reg[(addr - A_RTC_CNTL_STORE0) / 4];
        break;

    case A_RTC_CNTL_STORE4:
    case A_RTC_CNTL_STORE5:
    case A_RTC_CNTL_STORE6:
    case A_RTC_CNTL_STORE7:
        r = s->scratch_reg[(addr - A_RTC_CNTL_STORE4) / 4 + 4];
        break;
    }
    return r;
}

static void esp32_rtc_cntl_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32RtcCntlState *s = ESP32_RTC_CNTL(opaque);

    switch (addr) {
    case A_RTC_CNTL_STORE0:
    case A_RTC_CNTL_STORE1:
    case A_RTC_CNTL_STORE2:
    case A_RTC_CNTL_STORE3:
        s->scratch_reg[(addr - A_RTC_CNTL_STORE0) / 4] = value;
        break;

    case A_RTC_CNTL_STORE4:
    case A_RTC_CNTL_STORE5:
    case A_RTC_CNTL_STORE6:
    case A_RTC_CNTL_STORE7:
        s->scratch_reg[(addr - A_RTC_CNTL_STORE4) / 4 + 4] = value;
        break;
    }
}

static const MemoryRegionOps esp32_rtc_cntl_ops = {
    .read =  esp32_rtc_cntl_read,
    .write = esp32_rtc_cntl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_rtc_cntl_reset(DeviceState *dev)
{
    Esp32RtcCntlState *s = ESP32_RTC_CNTL(dev);
}

static void esp32_rtc_cntl_realize(DeviceState *dev, Error **errp)
{
    Esp32RtcCntlState *s = ESP32_RTC_CNTL(dev);
}

static void esp32_rtc_cntl_init(Object *obj)
{
    Esp32RtcCntlState *s = ESP32_RTC_CNTL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_rtc_cntl_ops, s,
                          TYPE_ESP32_RTC_CNTL, ESP32_RTC_CNTL_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property esp32_rtc_cntl_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_rtc_cntl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_rtc_cntl_reset;
    dc->realize = esp32_rtc_cntl_realize;
    dc->props = esp32_rtc_cntl_properties;
}

static const TypeInfo esp32_rtc_cntl_info = {
    .name = TYPE_ESP32_RTC_CNTL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32RtcCntlState),
    .instance_init = esp32_rtc_cntl_init,
    .class_init = esp32_rtc_cntl_class_init
};

static void esp32_rtc_cntl_register_types(void)
{
    type_register_static(&esp32_rtc_cntl_info);
}

type_init(esp32_rtc_cntl_register_types)

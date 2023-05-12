/*
 * ESP32-C3 USB Serial JTAG emulation
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
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c3_jtag.h"


static uint64_t esp32c3_jtag_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(opaque);
    (void) s;
    return 0;
}

static void esp32c3_jtag_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(opaque);
    (void) s;
}

static const MemoryRegionOps esp32c3_jtag_ops = {
    .read =  esp32c3_jtag_read,
    .write = esp32c3_jtag_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_jtag_reset(DeviceState *dev)
{
    (void) dev;
}

static void esp32c3_jtag_realize(DeviceState *dev, Error **errp)
{
    (void) dev;
    (void) errp;
}

static void esp32c3_jtag_init(Object *obj)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_jtag_ops, s,
                          TYPE_ESP32C3_JTAG, ESP32C3_JTAG_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c3_jtag_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_jtag_reset;
    dc->realize = esp32c3_jtag_realize;
}

static const TypeInfo esp32c3_jtag_info = {
    .name = TYPE_ESP32C3_JTAG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3UsbJtagState),
    .instance_init = esp32c3_jtag_init,
    .class_init = esp32c3_jtag_class_init
};

static void esp32c3_jtag_types(void)
{
    type_register_static(&esp32c3_jtag_info);
}

type_init(esp32c3_jtag_types)

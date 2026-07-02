/*
 * ESP32-C6 USB Serial JTAG emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/misc/esp32c6_jtag.h"


static uint64_t esp32c6_jtag_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t r = 0;

    switch (addr) {
    case USB_SERIAL_JTAG_EP1_REG:
        r = 0;
        break;
    case USB_SERIAL_JTAG_EP1_CONF_REG:
        /* SERIAL_IN_EP_DATA_FREE=1: TX FIFO ready to accept data */
        r = USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE;
        break;
    default:
        break;
    }
    return r;
}

static void esp32c6_jtag_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32C6UsbJtagState *s = ESP32C6_JTAG(opaque);

    switch (addr) {
    case USB_SERIAL_JTAG_EP1_REG:
        /* Byte written to UART TX FIFO - forward to chardev */
        if (qemu_chr_fe_backend_connected(&s->chr)) {
            uint8_t ch = value & 0xff;
            qemu_chr_fe_write(&s->chr, &ch, 1);
        }
        break;
    case USB_SERIAL_JTAG_EP1_CONF_REG:
        /* WR_DONE - no-op, we output immediately on byte write */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32c6_jtag_ops = {
    .read = esp32c6_jtag_read,
    .write = esp32c6_jtag_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_jtag_reset_hold(Object *obj, ResetType type)
{
    ESP32C6UsbJtagState *s = ESP32C6_JTAG(obj);
    (void) s;
}

static void esp32c6_jtag_realize(DeviceState *dev, Error **errp)
{
    ESP32C6UsbJtagState *s = ESP32C6_JTAG(dev);

    qemu_chr_fe_set_handlers(&s->chr, NULL, NULL, NULL, NULL, s, NULL, true);
}

static void esp32c6_jtag_init(Object *obj)
{
    ESP32C6UsbJtagState *s = ESP32C6_JTAG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_jtag_ops, s,
                          TYPE_ESP32C6_JTAG, ESP32C6_JTAG_REGS_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static Property esp32c6_jtag_properties[] = {
    DEFINE_PROP_CHR("chardev", ESP32C6UsbJtagState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c6_jtag_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c6_jtag_reset_hold;
    dc->realize = esp32c6_jtag_realize;
    device_class_set_props(dc, esp32c6_jtag_properties);
}

static const TypeInfo esp32c6_jtag_info = {
    .name = TYPE_ESP32C6_JTAG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6UsbJtagState),
    .instance_init = esp32c6_jtag_init,
    .class_init = esp32c6_jtag_class_init,
};

static void esp32c6_jtag_register_types(void)
{
    type_register_static(&esp32c6_jtag_info);
}

type_init(esp32c6_jtag_register_types)

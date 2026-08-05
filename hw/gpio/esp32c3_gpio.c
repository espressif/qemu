/*
 * ESP32-C3 GPIO emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32c3_gpio.h"

/* ESP32-C3 GPIO register offsets */
#define GPIO_OUT_REG        0x04
#define GPIO_OUT_W1TS_REG   0x08
#define GPIO_OUT_W1TC_REG   0x0C
#define GPIO_ENABLE_REG     0x20
#define GPIO_ENABLE_W1TS    0x24
#define GPIO_ENABLE_W1TC    0x28
#define GPIO_STRAP_REG      0x38
#define GPIO_IN_REG         0x3C

/* Debug output */
#define GPIO_DEBUG 0

static void esp32c3_gpio_update_outputs(ESP32C3GPIOState *s, uint32_t old_out)
{
    uint32_t changed = old_out ^ s->gpio_out;

    for (int i = 0; i < ESP32C3_GPIO_COUNT; i++) {
        if (changed & (1 << i)) {
            int level = (s->gpio_out >> i) & 1;
#if GPIO_DEBUG
            info_report("[GPIO] Pin %d -> %d", i, level);
#endif
            qemu_set_irq(s->gpio_out_irq[i], level);
        }
    }
}

static uint64_t esp32c3_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3GPIOState *s = ESP32C3_GPIO(opaque);
    Esp32GpioState *parent = &s->parent;
    uint64_t r = 0;

    switch (addr) {
    case GPIO_OUT_REG:
        r = s->gpio_out;
        break;
    case GPIO_ENABLE_REG:
        r = s->gpio_enable;
        break;
    case GPIO_STRAP_REG:
        r = parent->strap_mode;
        break;
    case GPIO_IN_REG:
        /* Return output values as input (loopback for now) */
        r = s->gpio_out;
        break;
    default:
        /* Many other GPIO registers exist; return 0 for now */
        break;
    }

#if GPIO_DEBUG
    if (addr != GPIO_IN_REG) {
        info_report("[GPIO] READ 0x%03lx = 0x%08lx", addr, r);
    }
#endif

    return r;
}

static void esp32c3_gpio_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned int size)
{
    ESP32C3GPIOState *s = ESP32C3_GPIO(opaque);
    uint32_t old_out = s->gpio_out;

#if GPIO_DEBUG
    info_report("[GPIO] WRITE 0x%03lx = 0x%08lx", addr, (unsigned long)value);
#endif

    switch (addr) {
    case GPIO_OUT_REG:
        s->gpio_out = value & ((1 << ESP32C3_GPIO_COUNT) - 1);
        esp32c3_gpio_update_outputs(s, old_out);
        break;
    case GPIO_OUT_W1TS_REG:
        s->gpio_out |= value & ((1 << ESP32C3_GPIO_COUNT) - 1);
        esp32c3_gpio_update_outputs(s, old_out);
        break;
    case GPIO_OUT_W1TC_REG:
        s->gpio_out &= ~(value & ((1 << ESP32C3_GPIO_COUNT) - 1));
        esp32c3_gpio_update_outputs(s, old_out);
        break;
    case GPIO_ENABLE_REG:
        s->gpio_enable = value & ((1 << ESP32C3_GPIO_COUNT) - 1);
        break;
    case GPIO_ENABLE_W1TS:
        s->gpio_enable |= value & ((1 << ESP32C3_GPIO_COUNT) - 1);
        break;
    case GPIO_ENABLE_W1TC:
        s->gpio_enable &= ~(value & ((1 << ESP32C3_GPIO_COUNT) - 1));
        break;
    default:
        /* Many other GPIO registers exist; ignore for now */
        break;
    }
}

static const MemoryRegionOps esp32c3_gpio_ops = {
    .read =  esp32c3_gpio_read,
    .write = esp32c3_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_gpio_reset_hold(Object *obj, ResetType type)
{
    ESP32C3GPIOState *s = ESP32C3_GPIO(obj);

    s->gpio_out = 0;
    s->gpio_enable = 0;

    /* Notify all outputs are now low */
    for (int i = 0; i < ESP32C3_GPIO_COUNT; i++) {
        qemu_set_irq(s->gpio_out_irq[i], 0);
    }
}

static void esp32c3_gpio_realize(DeviceState *dev, Error **errp)
{
    /* Parent realize is a no-op */
}

static void esp32c3_gpio_init(Object *obj)
{
    ESP32C3GPIOState *s = ESP32C3_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Set the default value for the property */
    object_property_set_int(obj, "strap_mode", ESP32C3_STRAP_MODE_FLASH_BOOT, &error_fatal);

    /* Override parent's iomem with our extended implementation */
    memory_region_init_io(&s->parent.iomem, obj, &esp32c3_gpio_ops, s,
                          TYPE_ESP32C3_GPIO, 0x1000);
    sysbus_init_mmio(sbd, &s->parent.iomem);

    /* Initialize output IRQs for each GPIO pin */
    qdev_init_gpio_out_named(DEVICE(s), s->gpio_out_irq,
                             ESP32C3_GPIO_OUT_IRQ, ESP32C3_GPIO_COUNT);

    s->gpio_out = 0;
    s->gpio_enable = 0;
}

static void esp32c3_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c3_gpio_reset_hold;
    dc->realize = esp32c3_gpio_realize;
}

static const TypeInfo esp32c3_gpio_info = {
    .name = TYPE_ESP32C3_GPIO,
    .parent = TYPE_ESP32_GPIO,
    .instance_size = sizeof(ESP32C3GPIOState),
    .instance_init = esp32c3_gpio_init,
    .class_init = esp32c3_gpio_class_init,
    .class_size = sizeof(ESP32C3GPIOClass),
};

static void esp32c3_gpio_register_types(void)
{
    type_register_static(&esp32c3_gpio_info);
}

type_init(esp32c3_gpio_register_types)

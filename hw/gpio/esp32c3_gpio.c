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
#define GPIO_FUNC_IN_SEL_CFG_FIRST  0x154
#define GPIO_FUNC_IN_SEL_CFG_LAST   \
    (GPIO_FUNC_IN_SEL_CFG_FIRST + (ESP32C3_GPIO_SIG_COUNT - 1) * 4)
#define GPIO_FUNC_OUT_SEL_CFG_FIRST 0x554
#define GPIO_FUNC_OUT_SEL_CFG_LAST  \
    (GPIO_FUNC_OUT_SEL_CFG_FIRST + (ESP32C3_GPIO_COUNT - 1) * 4)

/*
 * GPIO_FUNCn_IN_SEL_CFG: IN_SEL is [4:0], IN_INV_SEL is bit 5 and SIG_IN_SEL
 * (bit 6) selects the GPIO matrix over the direct IOMUX connection. The pad
 * numbers 0x1e and 0x1f stand for constant one and constant zero rather than
 * a pad, and are rejected by the ESP32C3_GPIO_COUNT bound below.
 */
#define GPIO_FUNC_IN_SEL_MASK   0x1f
#define GPIO_FUNC_SIG_IN_SEL    (1 << 6)

/* GPIO_FUNC<pad>_OUT_SEL_CFG.OUT_SEL, the signal driving that pad */
#define GPIO_FUNC_OUT_SEL_MASK  0xff

/* Debug output */
#define GPIO_DEBUG 0

int esp32c3_gpio_signal_pad(ESP32C3GPIOState *s, unsigned sig)
{
    if (sig >= ESP32C3_GPIO_SIG_COUNT) {
        return -1;
    }

    /* Direct lookup: the in-sel table is indexed by signal */
    uint32_t cfg = s->func_in_sel_cfg[sig];

    if ((cfg & GPIO_FUNC_SIG_IN_SEL) &&
        (cfg & GPIO_FUNC_IN_SEL_MASK) < ESP32C3_GPIO_COUNT) {
        return cfg & GPIO_FUNC_IN_SEL_MASK;
    }

    /*
     * Fall back to the out-sel table, which is indexed by pad and so needs a
     * scan. s_hp_i2c_pins_config() writes both directions, but a peripheral
     * configured for output only would appear here alone.
     */
    for (unsigned pad = 0; pad < ESP32C3_GPIO_COUNT; pad++) {
        if ((s->func_out_sel_cfg[pad] & GPIO_FUNC_OUT_SEL_MASK) == sig) {
            return pad;
        }
    }

    /*
     * Unrouted. i2c_common_deinit_pins() resets the in-sel entry to
     * GPIO_MATRIX_CONST_ZERO_INPUT, so this is also what a closed bus handle
     * looks like.
     */
    return -1;
}

int esp32c3_gpio_pad_out_signal(ESP32C3GPIOState *s, unsigned pad)
{
    if (pad >= ESP32C3_GPIO_COUNT) {
        return -1;
    }
    return s->func_out_sel_cfg[pad] & GPIO_FUNC_OUT_SEL_MASK;
}

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
    case GPIO_FUNC_IN_SEL_CFG_FIRST ... GPIO_FUNC_IN_SEL_CFG_LAST:
        r = s->func_in_sel_cfg[(addr - GPIO_FUNC_IN_SEL_CFG_FIRST) / 4];
        break;
    case GPIO_FUNC_OUT_SEL_CFG_FIRST ... GPIO_FUNC_OUT_SEL_CFG_LAST:
        r = s->func_out_sel_cfg[(addr - GPIO_FUNC_OUT_SEL_CFG_FIRST) / 4];
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
    case GPIO_FUNC_IN_SEL_CFG_FIRST ... GPIO_FUNC_IN_SEL_CFG_LAST:
        /*
         * The signal routing itself is not simulated, but peripherals need to
         * be able to tell which pads they are currently wired to.
         */
        s->func_in_sel_cfg[(addr - GPIO_FUNC_IN_SEL_CFG_FIRST) / 4] = value;
        break;
    case GPIO_FUNC_OUT_SEL_CFG_FIRST ... GPIO_FUNC_OUT_SEL_CFG_LAST:
        s->func_out_sel_cfg[(addr - GPIO_FUNC_OUT_SEL_CFG_FIRST) / 4] = value;
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
    memset(s->func_in_sel_cfg, 0, sizeof(s->func_in_sel_cfg));
    for (int i = 0; i < ESP32C3_GPIO_COUNT; i++) {
        /* Reset default is SIG_GPIO_OUT_IDX: driven by GPIO, not a signal */
        s->func_out_sel_cfg[i] = 0x80;
    }

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

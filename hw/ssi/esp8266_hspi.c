/*
 * ESP8266 HSPI (SPI1, 0x60000100): user-command transfers through W0..W15
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/ssi/esp8266_hspi.h"

#define ESP8266_HSPI_CMD_USR        BIT(18)
#define ESP8266_HSPI_USER_MOSI      BIT(27)
#define ESP8266_HSPI_USER_MISO      BIT(28)
#define ESP8266_HSPI_W_BYTES        64

static void esp8266_hspi_transfer(Esp8266HspiState *s)
{
    uint32_t user = s->reg[ESP8266_HSPI_USER];
    uint32_t user1 = s->reg[ESP8266_HSPI_USER1];
    unsigned bits = 0;
    uint8_t *w = (uint8_t *)&s->reg[ESP8266_HSPI_W0];

    if (user & ESP8266_HSPI_USER_MOSI) {
        bits = extract32(user1, 17, 9) + 1;
    } else if (user & ESP8266_HSPI_USER_MISO) {
        bits = extract32(user1, 8, 9) + 1;
    }
    /*
     * shortcut: byte-granular, full duplex, little-endian W buffer (as
     * ExpressLRS and the Arduino SPI library use it); no address,
     * command or dummy phases.
     */
    qemu_set_irq(s->cs, 0);
    for (unsigned i = 0; i < MIN(DIV_ROUND_UP(bits, 8), ESP8266_HSPI_W_BYTES);
         i++) {
        w[i] = ssi_transfer(s->bus, w[i]);
    }
    qemu_set_irq(s->cs, 1);
}

static uint64_t esp8266_hspi_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp8266HspiState *s = opaque;

    return addr / 4 < ESP8266_HSPI_REGS ? s->reg[addr / 4] : 0;
}

static void esp8266_hspi_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    Esp8266HspiState *s = opaque;

    if (addr / 4 >= ESP8266_HSPI_REGS) {
        return;
    }
    s->reg[addr / 4] = val;
    if (addr / 4 == ESP8266_HSPI_CMD && (val & ESP8266_HSPI_CMD_USR)) {
        esp8266_hspi_transfer(s);
        s->reg[ESP8266_HSPI_CMD] &= ~ESP8266_HSPI_CMD_USR;  /* done at once */
    }
}

static const MemoryRegionOps esp8266_hspi_ops = {
    .read = esp8266_hspi_read,
    .write = esp8266_hspi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void esp8266_hspi_init(Object *obj)
{
    Esp8266HspiState *s = ESP8266_HSPI(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_hspi_ops, s,
                          TYPE_ESP8266_HSPI, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    s->bus = ssi_create_bus(DEVICE(obj), "spi");
    qdev_init_gpio_out_named(DEVICE(obj), &s->cs, SSI_GPIO_CS, 1);
}

static void esp8266_hspi_reset_hold(Object *obj, ResetType type)
{
    Esp8266HspiState *s = ESP8266_HSPI(obj);

    memset(s->reg, 0, sizeof(s->reg));
}

static void esp8266_hspi_reset_exit(Object *obj, ResetType type)
{
    qemu_set_irq(ESP8266_HSPI(obj)->cs, 1);
}

static const VMStateDescription vmstate_esp8266_hspi = {
    .name = TYPE_ESP8266_HSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, Esp8266HspiState, ESP8266_HSPI_REGS),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_hspi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_esp8266_hspi;
    rc->phases.hold = esp8266_hspi_reset_hold;
    rc->phases.exit = esp8266_hspi_reset_exit;
}

static const TypeInfo esp8266_hspi_info = {
    .name = TYPE_ESP8266_HSPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266HspiState),
    .instance_init = esp8266_hspi_init,
    .class_init = esp8266_hspi_class_init,
};

static void esp8266_hspi_register_types(void)
{
    type_register_static(&esp8266_hspi_info);
}

type_init(esp8266_hspi_register_types)

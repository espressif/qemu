/*
 * ESP32 GPIO emulation
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
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32_gpio.h"

#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qnum.h"

// #define ENABLE_DEBUG

#ifdef ENABLE_DEBUG
#define D(x) x
#else
#define D(x) do {} while(0)
#endif

/*
To start a tcp server of esp32.gpio:
-chardev socket,port=17001,wait=no,host=0.0.0.0,server=on,id=esp32.gpio \
-global driver=esp32.gpio,property=chardev,value=esp32.gpio \

Event when output gpio(s) were modified:
{"event":"new_out", "out":0, "out1":0}\r\n

Message to set new value of input gpio(s):
{"cmd":"set_input", "in":2147483648, "in1":1}\r\n
{"cmd":"set_input", "in":16}\r\n

4.12.1 GPIO Matrix Register Summary

bitmask: each bit corresponds to one GPIO

"in": GPIO31-GPIO0
"in":2147483648 -> 0x80000000 -> MSB corresponds to GPIO31
"in":1 -> 0x00000001 -> LSB corresponds to GPIO0

"in1": GPIO39-GPIO32
"in1": 128 -> 0x0x00000080 -> GPIO39
"in1": 1 -> 0x0x00000001 -> GPIO32

TODO:
- improve handling of gpio_in_reg and gpio_in1_reg registers
- should a firmware be able to change a state of GPIO when not enabled?
*/

#define HEX32_FMT "%08x"
#define HEX64_FMT "%016lx"

#define CRLF "\r\n"

static void set_uint32_reg(QNum *num, uint32_t *reg, uint32_t mask)
{
    if(num != NULL && reg != NULL)
    {
        uint64_t value;
        if(qnum_get_try_uint(num, &value))
        {
            D(qemu_log("server_read: reg=0x"HEX64_FMT"\n", value));

            *reg = (uint32_t) value & mask;
        }
    }
}

static void server_read(void *opaque, const uint8_t *buf, int size)
{
    Esp32GpioState *s = ESP32_GPIO(opaque);

    QEMU_LOCK_GUARD(&s->mutex);

    /* find CRLF in buf */
    char* crlf_pos = g_strstr_len((const char* ) buf, size, CRLF);
    if(crlf_pos != NULL)
    {
        /* calc length */
        uint8_t len = (const char*) crlf_pos - (const char*) buf; 

        /* prepare string */
        g_autoptr(GString) msg = g_string_new_len((const char*) buf, len);

        /* decode json */
        Error *err = NULL;
        QDict *dict = qobject_to(QDict, qobject_from_json(msg->str, &err));
        if (dict)
        {
            const char * cmd = qdict_get_try_str(dict, "cmd");
            if(cmd != NULL)
            {
                if(strcmp(cmd, "set_input") == 0)
                {
                    set_uint32_reg(qobject_to(QNum, qdict_get(dict, "in")), &s->gpio_in_reg, 0xFFFFFFFF);
                    set_uint32_reg(qobject_to(QNum, qdict_get(dict, "in1")), &s->gpio_in1_reg, 0x0000000F);
                }
            }

            qobject_unref(dict);
        }
    }

    /* ignore rest data of the buffer. Is it okay? */
}


static int server_can_read(void *opaque)
{
    return 256;
}

static void server_event(void *opaque, QEMUChrEvent event)
{
    switch (event)
    {
    case CHR_EVENT_OPENED:
        D(qemu_log("esp32_gpio_server: client connected\n"));
        break;
    case CHR_EVENT_CLOSED:
        D(qemu_log("esp32_gpio_server: client disconnected\n"));
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        break;
    }
}

static uint64_t esp32_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    D(qemu_log("esp32_gpio_read: addr=0x"HEX64_FMT"\n", addr));

    Esp32GpioState *s = ESP32_GPIO(opaque);

    QEMU_LOCK_GUARD(&s->mutex);

    uint64_t r = 0;
    switch (addr) {
    case A_GPIO_STRAP:
        r = s->strap_mode;
        break;
    case A_GPIO_IN_REG:
        r = s->gpio_in_reg;
        break;
    case A_GPIO_IN1_REG:
        r = s->gpio_in1_reg;
        break;
    default:
        break;
    }

    return r;
}

static void esp32_gpio_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    D(qemu_log("esp32_gpio_write: addr=0x"HEX64_FMT", value=0x"HEX64_FMT"\n", addr, value));

    Esp32GpioState *s = ESP32_GPIO(opaque);

    QEMU_LOCK_GUARD(&s->mutex);

    uint32_t value32 = (uint32_t) value;

    switch (addr) {
    case A_GPIO_OUT_W1TS_REG:
        s->gpio_out_reg |= value32;
        break;
    case A_GPIO_OUT_W1TC_REG:
        s->gpio_out_reg &= ~value32;
        break;
    case A_GPIO_OUT1_W1TS_REG:
        s->gpio_out1_reg |= value32;
        break;
    case A_GPIO_OUT1_W1TC_REG:
        s->gpio_out1_reg &= ~value32;
        break;
    case A_GPIO_ENABLE_W1TS_REG:
        s->gpio_enable_reg |= value32;
        break;
    case A_GPIO_ENABLE_W1TC_REG:
        s->gpio_enable_reg &= ~value32;
        break;
    case A_GPIO_ENABLE1_W1TS_REG:
        s->gpio_enable1_reg |= value32;
        break;
    case A_GPIO_ENABLE1_W1TC_REG:
        s->gpio_enable1_reg &= ~value32;
        break;
    default:
        break;
    }

    if(s->chardev)
    {
        g_autoptr(QDict) dict = qdict_new();
        qdict_put_str(dict, "event", "new_out");
        qdict_put_int(dict, "out", s->gpio_out_reg);
        qdict_put_int(dict, "out1", s->gpio_out1_reg);

        g_autoptr(GString) json = qobject_to_json(QOBJECT(dict));
        g_string_append(json, CRLF);

        qemu_chr_fe_write_all(&s->charbe, (uint8_t *)json->str, json->len);
    }
}

static const MemoryRegionOps uart_ops = {
    .read =  esp32_gpio_read,
    .write = esp32_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_gpio_reset(DeviceState *dev)
{
}

static void esp32_gpio_realize(DeviceState *dev, Error **errp)
{
    Esp32GpioState *s = ESP32_GPIO(dev);

    if(s->chardev_name)
    {
        D(qemu_log("esp32_gpio_server: chardev_name=%s\n", s->chardev_name));

        s->chardev = qemu_chr_find(s->chardev_name);
        if (!s->chardev)
        {
            error_report("chardev '%s' not found", s->chardev_name);
            return;
        }

        qemu_chr_fe_init(&s->charbe, s->chardev, &error_abort);
        qemu_chr_fe_set_handlers(&s->charbe, server_can_read, server_read, server_event, NULL, dev, NULL, true);
    }
}

static void esp32_gpio_init(Object *obj)
{
    Esp32GpioState *s = ESP32_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s,
                          TYPE_ESP32_GPIO, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qemu_mutex_init(&s->mutex);
}

static Property esp32_gpio_properties[] = {
    DEFINE_PROP_UINT32("strap_mode", Esp32GpioState, strap_mode, ESP32_STRAP_MODE_FLASH_BOOT),
    DEFINE_PROP_STRING("chardev", Esp32GpioState, chardev_name),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_gpio_reset;
    dc->realize = esp32_gpio_realize;
    device_class_set_props(dc, esp32_gpio_properties);
}

static const TypeInfo esp32_gpio_info = {
    .name = TYPE_ESP32_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32GpioState),
    .instance_init = esp32_gpio_init,
    .class_init = esp32_gpio_class_init
};

static void esp32_gpio_register_types(void)
{
    type_register_static(&esp32_gpio_info);
}

type_init(esp32_gpio_register_types)

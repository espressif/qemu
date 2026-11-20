/*
 * Copyright (c) 2014, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ESP8266 GPIO (0x60000300), based on Max Filippov's ESP8266 model from
 * OSLL/qemu-xtensa (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/gpio/esp8266_gpio.h"

#define ESP8266_GPIO_STRAP_SD_START     (0x4 << 16)
#define ESP8266_GPIO_STRAP_FLASH_START  (0x3 << 16)
#define ESP8266_GPIO_STRAP_UART_START   (0x2 << 16)

enum {
    ESP8266_GPIO_OUT = 0x00,
    ESP8266_GPIO_OUT_SET = 0x04,
    ESP8266_GPIO_OUT_CLEAR = 0x08,
    ESP8266_GPIO_ENABLE = 0x0c,
    ESP8266_GPIO_ENABLE_SET = 0x10,
    ESP8266_GPIO_ENABLE_CLEAR = 0x14,
    ESP8266_GPIO_IN = 0x18,
    ESP8266_GPIO_STATUS = 0x1c,
    ESP8266_GPIO_STATUS_SET = 0x20,
    ESP8266_GPIO_STATUS_CLEAR = 0x24,
    ESP8266_GPIO_CONF = 0x28,           /* 16 words */
};

/* GPIO_CONF: interrupt type in bits 7..9 */
enum {
    ESP8266_GPIO_INT_NONE,
    ESP8266_GPIO_INT_POSEDGE,
    ESP8266_GPIO_INT_NEGEDGE,
    ESP8266_GPIO_INT_ANYEDGE,
    ESP8266_GPIO_INT_LOW,
    ESP8266_GPIO_INT_HIGH,
};

static uint32_t esp8266_gpio_levels(Esp8266GpioState *s)
{
    return ((s->out & s->enable) | (s->pins & ~s->enable)) & 0xffff;
}

static void esp8266_gpio_update(Esp8266GpioState *s)
{
    uint32_t levels = esp8266_gpio_levels(s);

    /* Level-type interrupts stay pending while the level persists. */
    for (int i = 0; i < ESP8266_GPIO_PINS; i++) {
        unsigned type = extract32(s->conf[i], 7, 3);

        if ((type == ESP8266_GPIO_INT_LOW && !(levels & BIT(i))) ||
            (type == ESP8266_GPIO_INT_HIGH && (levels & BIT(i)))) {
            s->status |= BIT(i);
        }
    }
    qemu_set_irq(s->irq, s->status != 0);
}

static void esp8266_gpio_set_outputs(Esp8266GpioState *s, uint32_t before)
{
    uint32_t after = esp8266_gpio_levels(s);

    for (int i = 0; i < ESP8266_GPIO_PINS; i++) {
        if ((before ^ after) & BIT(i)) {
            qemu_set_irq(s->output[i], !!(after & BIT(i)));
        }
    }
}

static void esp8266_gpio_input(void *opaque, int pin, int level)
{
    Esp8266GpioState *s = opaque;
    uint32_t before = esp8266_gpio_levels(s);
    unsigned type = extract32(s->conf[pin], 7, 3);
    bool rose, fell;

    s->pins = deposit32(s->pins, pin, 1, !!level);
    rose = !(before & BIT(pin)) && (esp8266_gpio_levels(s) & BIT(pin));
    fell = (before & BIT(pin)) && !(esp8266_gpio_levels(s) & BIT(pin));
    if ((rose && (type == ESP8266_GPIO_INT_POSEDGE ||
                  type == ESP8266_GPIO_INT_ANYEDGE)) ||
        (fell && (type == ESP8266_GPIO_INT_NEGEDGE ||
                  type == ESP8266_GPIO_INT_ANYEDGE))) {
        s->status |= BIT(pin);
    }
    esp8266_gpio_update(s);
}

static uint64_t esp8266_gpio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    Esp8266GpioState *s = opaque;

    switch (addr) {
    case ESP8266_GPIO_OUT:
        return s->out;
    case ESP8266_GPIO_ENABLE:
        return s->enable;
    case ESP8266_GPIO_IN:
        /* Bits 16.. carry the boot strapping (flash boot). */
        return esp8266_gpio_levels(s) | ESP8266_GPIO_STRAP_FLASH_START |
               (0x7 << 29);
    case ESP8266_GPIO_STATUS:
        return s->status;
    default:
        if (addr >= ESP8266_GPIO_CONF &&
            addr < ESP8266_GPIO_CONF + 4 * ESP8266_GPIO_PINS) {
            return s->conf[(addr - ESP8266_GPIO_CONF) / 4];
        }
        qemu_log_mask(LOG_UNIMP, "%s: +0x%x\n", __func__, (uint32_t)addr);
        return 0;
    }
}

static void esp8266_gpio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    Esp8266GpioState *s = opaque;
    uint32_t before = esp8266_gpio_levels(s);

    val &= 0xffff;
    switch (addr) {
    case ESP8266_GPIO_OUT:
        s->out = val;
        break;
    case ESP8266_GPIO_OUT_SET:
        s->out |= val;
        break;
    case ESP8266_GPIO_OUT_CLEAR:
        s->out &= ~val;
        break;
    case ESP8266_GPIO_ENABLE:
        s->enable = val;
        break;
    case ESP8266_GPIO_ENABLE_SET:
        s->enable |= val;
        break;
    case ESP8266_GPIO_ENABLE_CLEAR:
        s->enable &= ~val;
        break;
    case ESP8266_GPIO_STATUS:
        s->status = val;
        break;
    case ESP8266_GPIO_STATUS_SET:
        s->status |= val;
        break;
    case ESP8266_GPIO_STATUS_CLEAR:
        s->status &= ~val;
        break;
    default:
        if (addr >= ESP8266_GPIO_CONF &&
            addr < ESP8266_GPIO_CONF + 4 * ESP8266_GPIO_PINS) {
            s->conf[(addr - ESP8266_GPIO_CONF) / 4] = val;
        } else {
            qemu_log_mask(LOG_UNIMP, "%s: +0x%x = 0x%x\n", __func__,
                          (uint32_t)addr, (uint32_t)val);
        }
        break;
    }
    esp8266_gpio_set_outputs(s, before);
    esp8266_gpio_update(s);
}

static const MemoryRegionOps esp8266_gpio_ops = {
    .read = esp8266_gpio_read,
    .write = esp8266_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_gpio_reset_hold(Object *obj, ResetType type)
{
    Esp8266GpioState *s = ESP8266_GPIO(obj);

    s->out = s->enable = s->status = 0;
    memset(s->conf, 0, sizeof(s->conf));
    esp8266_gpio_update(s);
}

static void esp8266_gpio_init(Object *obj)
{
    Esp8266GpioState *s = ESP8266_GPIO(obj);

    /* Undriven inputs read high (pull-ups, buttons released); not reset. */
    s->pins = 0xffff;
    memory_region_init_io(&s->iomem, obj, &esp8266_gpio_ops, s,
                          TYPE_ESP8266_GPIO, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in(DEVICE(obj), esp8266_gpio_input, ESP8266_GPIO_PINS);
    qdev_init_gpio_out(DEVICE(obj), s->output, ESP8266_GPIO_PINS);
}

static const VMStateDescription vmstate_esp8266_gpio = {
    .name = TYPE_ESP8266_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(out, Esp8266GpioState),
        VMSTATE_UINT32(enable, Esp8266GpioState),
        VMSTATE_UINT32(pins, Esp8266GpioState),
        VMSTATE_UINT32(status, Esp8266GpioState),
        VMSTATE_UINT32_ARRAY(conf, Esp8266GpioState, ESP8266_GPIO_PINS),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_esp8266_gpio;
    rc->phases.hold = esp8266_gpio_reset_hold;
}

static const TypeInfo esp8266_gpio_info = {
    .name = TYPE_ESP8266_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266GpioState),
    .instance_init = esp8266_gpio_init,
    .class_init = esp8266_gpio_class_init,
};

static void esp8266_gpio_register_types(void)
{
    type_register_static(&esp8266_gpio_info);
}

type_init(esp8266_gpio_register_types)

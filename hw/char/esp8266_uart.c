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
 * ESP8266 UART0 (0x60000000), based on Max Filippov's ESP8266 model from
 * OSLL/qemu-xtensa (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "hw/char/esp8266_uart.h"



FIELD(UART_INT, RXFIFO_FULL, 0, 1)
FIELD(UART_INT, TXFIFO_EMPTY, 1, 1)
FIELD(UART_INT, RXFIFO_OVF, 4, 1)
FIELD(UART_CONF0, LOOPBACK, 14, 1)
FIELD(UART_CONF0, RXFIFO_RST, 17, 1)
FIELD(UART_CONF1, RXFIFO_FULL, 0, 7)

#define ESP8266_UART_FIFO_MASK (ESP8266_UART_FIFO_SIZE - 1)

static unsigned esp8266_uart_rx_fifo_size(Esp8266UartState *s)
{
    return (s->rx_last - s->rx_first) & ESP8266_UART_FIFO_MASK;
}

static int esp8266_uart_can_receive(void *opaque)
{
    Esp8266UartState *s = opaque;

    return esp8266_uart_rx_fifo_size(s) != ESP8266_UART_FIFO_SIZE - 1;
}

static void esp8266_uart_irq_update(Esp8266UartState *s)
{
    s->reg[ESP8266_UART_INT_ST] |= s->reg[ESP8266_UART_INT_RAW];
    if (s->reg[ESP8266_UART_INT_ST] & s->reg[ESP8266_UART_INT_ENA]) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void esp8266_uart_rx_irq_update(Esp8266UartState *s)
{
    if (esp8266_uart_rx_fifo_size(s) >=
        FIELD_EX32(s->reg[ESP8266_UART_CONF1], UART_CONF1, RXFIFO_FULL)) {
        s->reg[ESP8266_UART_INT_RAW] |= R_UART_INT_RXFIFO_FULL_MASK;
    } else {
        s->reg[ESP8266_UART_INT_RAW] &= ~R_UART_INT_RXFIFO_FULL_MASK;
    }

    if (!esp8266_uart_can_receive(s)) {
        s->reg[ESP8266_UART_INT_RAW] |= R_UART_INT_RXFIFO_OVF_MASK;
    } else {
        s->reg[ESP8266_UART_INT_RAW] &= ~R_UART_INT_RXFIFO_OVF_MASK;
    }
    esp8266_uart_irq_update(s);
}

static uint64_t esp8266_uart_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    Esp8266UartState *s = opaque;

    if ((addr & 3) || size != 4) {
        return 0;
    }

    switch (addr / 4) {
    case ESP8266_UART_STATUS:
        return esp8266_uart_rx_fifo_size(s);

    case ESP8266_UART_FIFO:
        if (esp8266_uart_rx_fifo_size(s)) {
            uint8_t r = s->rx[s->rx_first++];

            s->rx_first &= ESP8266_UART_FIFO_MASK;
            esp8266_uart_rx_irq_update(s);
            return r;
        } else {
            return 0;
        }
    case ESP8266_UART_INT_RAW:
    case ESP8266_UART_INT_ST:
    case ESP8266_UART_INT_ENA:
    case ESP8266_UART_INT_CLR:
    case ESP8266_UART_CLKDIV:
    case ESP8266_UART_AUTOBAUD:
    case ESP8266_UART_CONF0:
    case ESP8266_UART_CONF1:
    case ESP8266_UART_LOWPULSE:
    case ESP8266_UART_HIGHPULSE:
    case ESP8266_UART_RXD_CNT:
    case ESP8266_UART_DATE:
    case ESP8266_UART_ID:
        return s->reg[addr / 4];

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected read @0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    return 0;
}

static void esp8266_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    Esp8266UartState *s = opaque;
    unsigned i;

    for (i = 0; i < size && esp8266_uart_can_receive(s); ++i) {
        s->rx[s->rx_last++] = buf[i];
        s->rx_last &= ESP8266_UART_FIFO_MASK;
    }

    esp8266_uart_rx_irq_update(s);
}

static void esp8266_uart_ro(Esp8266UartState *s, hwaddr addr,
                              uint64_t val, unsigned size)
{
}

static void esp8266_uart_tx(Esp8266UartState *s, hwaddr addr,
                              uint64_t val, unsigned size)
{
    if (FIELD_EX32(s->reg[ESP8266_UART_CONF0], UART_CONF0, LOOPBACK)) {
        if (esp8266_uart_can_receive(s)) {
            uint8_t buf[] = { (uint8_t)val };

            esp8266_uart_receive(s, buf, 1);
        }

    } else if (qemu_chr_fe_backend_connected(&s->chr)) {
        uint8_t buf[1] = { val };
        qemu_chr_fe_write_all(&s->chr, buf, 1);
        s->reg[ESP8266_UART_INT_RAW] |= R_UART_INT_TXFIFO_EMPTY_MASK;
        esp8266_uart_irq_update(s);
    }
}

static void esp8266_uart_int_ena(Esp8266UartState *s, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    s->reg[ESP8266_UART_INT_ENA] = val & 0x1ff;
    esp8266_uart_irq_update(s);
}

static void esp8266_uart_int_clr(Esp8266UartState *s, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    s->reg[ESP8266_UART_INT_ST] &= ~val & 0x1ff;
    esp8266_uart_irq_update(s);
}

static void esp8266_uart_set_conf0(Esp8266UartState *s, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    s->reg[ESP8266_UART_CONF0] = val & 0xffffff;
    if (FIELD_EX32(s->reg[ESP8266_UART_CONF0], UART_CONF0, RXFIFO_RST)) {
        s->rx_first = s->rx_last = 0;
        esp8266_uart_rx_irq_update(s);
    }
}

static void esp8266_uart_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    Esp8266UartState *s = opaque;
    static void (* const handler[])(Esp8266UartState *s, hwaddr addr,
                                    uint64_t val, unsigned size) = {
        [ESP8266_UART_FIFO] = esp8266_uart_tx,
        [ESP8266_UART_INT_RAW] = esp8266_uart_ro,
        [ESP8266_UART_INT_ST] = esp8266_uart_ro,
        [ESP8266_UART_INT_ENA] = esp8266_uart_int_ena,
        [ESP8266_UART_INT_CLR] = esp8266_uart_int_clr,
        [ESP8266_UART_STATUS] = esp8266_uart_ro,
        [ESP8266_UART_CONF0] = esp8266_uart_set_conf0,
        [ESP8266_UART_LOWPULSE] = esp8266_uart_ro,
        [ESP8266_UART_HIGHPULSE] = esp8266_uart_ro,
        [ESP8266_UART_RXD_CNT] = esp8266_uart_ro,
    };

    if ((addr & 3) || size != 4 || addr / 4 >= ESP8266_UART_MAX) {
        return;
    }

    if (addr / 4 < ARRAY_SIZE(handler) && handler[addr / 4]) {
        handler[addr / 4](s, addr, val, size);
    } else {
        s->reg[addr / 4] = val;
    }
}

static const MemoryRegionOps esp8266_uart_ops = {
    .read = esp8266_uart_read,
    .write = esp8266_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_uart_reset_hold(Object *obj, ResetType type)
{
    Esp8266UartState *s = ESP8266_UART(obj);

    memset(s->reg, 0, sizeof(s->reg));
    s->rx_first = s->rx_last = 0;

    s->reg[ESP8266_UART_CLKDIV] = 0x2b6;
    s->reg[ESP8266_UART_AUTOBAUD] = 0x1000;
    s->reg[ESP8266_UART_CONF0] = 0x1c;
    s->reg[ESP8266_UART_CONF1] = 0x6060;
    s->reg[ESP8266_UART_LOWPULSE] = 0xfffff;
    s->reg[ESP8266_UART_HIGHPULSE] = 0xfffff;
    s->reg[ESP8266_UART_DATE] = 0x62000;
    s->reg[ESP8266_UART_ID] = 0x500;

    esp8266_uart_irq_update(s);
}

static void esp8266_uart_realize(DeviceState *dev, Error **errp)
{
    Esp8266UartState *s = ESP8266_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, esp8266_uart_can_receive,
                             esp8266_uart_receive, NULL, NULL, s, NULL, true);
}

static void esp8266_uart_init(Object *obj)
{
    Esp8266UartState *s = ESP8266_UART(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_uart_ops, s,
                          TYPE_ESP8266_UART, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_esp8266_uart = {
    .name = TYPE_ESP8266_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(rx_first, Esp8266UartState),
        VMSTATE_UINT32(rx_last, Esp8266UartState),
        VMSTATE_UINT8_ARRAY(rx, Esp8266UartState, ESP8266_UART_FIFO_SIZE),
        VMSTATE_UINT32_ARRAY(reg, Esp8266UartState, ESP8266_UART_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static Property esp8266_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", Esp8266UartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp8266_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp8266_uart_realize;
    dc->vmsd = &vmstate_esp8266_uart;
    rc->phases.hold = esp8266_uart_reset_hold;
    device_class_set_props(dc, esp8266_uart_properties);
}

static const TypeInfo esp8266_uart_info = {
    .name = TYPE_ESP8266_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266UartState),
    .instance_init = esp8266_uart_init,
    .class_init = esp8266_uart_class_init,
};

static void esp8266_uart_register_types(void)
{
    type_register_static(&esp8266_uart_info);
}

type_init(esp8266_uart_register_types)

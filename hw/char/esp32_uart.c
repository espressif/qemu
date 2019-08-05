/*
 * ESP32 UART emulation
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * The QEMU model of nRF51 UART by Julia Suvorova was used as a template.
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
#include "sysemu/sysemu.h"
#include "chardev/char-fe.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/char/esp32_uart.h"
#include "trace.h"

static void fifo_put(ESP32UARTFIFO *q, uint8_t ch)
{
    q->data[q->w_index] = ch;
    q->w_index = (q->w_index + 1) % UART_FIFO_LENGTH;
}

static uint8_t fifo_get(ESP32UARTFIFO *q)
{
    uint8_t ret = q->data[q->r_index];
    q->r_index = (q->r_index + 1) % UART_FIFO_LENGTH;
    return  ret;
}

static int fifo_count(const ESP32UARTFIFO *q)
{
    if (q->w_index < q->r_index) {
        return UART_FIFO_LENGTH - q->r_index + q->w_index;
    }

    return q->w_index - q->r_index;
}

static int fifo_avail(const ESP32UARTFIFO *q)
{
    return UART_FIFO_LENGTH - fifo_count(q);
}

static void fifo_reset(ESP32UARTFIFO *q)
{
    q->w_index = 0;
    q->r_index = 0;
}


static gboolean uart_transmit(GIOChannel *chan, GIOCondition cond, void *opaque);
static void uart_receive(void *opaque, const uint8_t *buf, int size);


static void esp_uart_update_irq(ESP32UARTState *s)
{
    bool irq = false;

    uint32_t int_st = s->reg[R_UART_INT_RAW] & s->reg[R_UART_INT_ENA];
    irq = int_st != 0;
    s->reg[R_UART_INT_ST] |= int_st;

    qemu_set_irq(s->irq, irq);
}

static uint64_t uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    uint64_t r = 0;

    switch (addr) {
    case A_UART_FIFO:
        if (fifo_count(&s->rx_fifo) == 0) {
            r = 0xEE;
            error_report("esp_uart: read UART FIFO while it is empty");
        } else {
            r = fifo_get(&s->rx_fifo);
            /* TODO: update IRQ status */
            qemu_chr_fe_accept_input(&s->chr);
        }
        break;

    case A_UART_STATUS:
        r = FIELD_DP32(r, UART_STATUS, RXFIFO_CNT, fifo_count(&s->rx_fifo));
        r = FIELD_DP32(r, UART_STATUS, TXFIFO_CNT, fifo_count(&s->tx_fifo));
        break;

    default:
        r = s->reg[addr / 4];
        break;
    }

    return r;
}


static void uart_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32UARTState *s = ESP32_UART(opaque);

    switch (addr) {
    case A_UART_FIFO:
        if (fifo_avail(&s->tx_fifo) == 0) {
            error_report("esp_uart: read UART FIFO while it is full");
        } else {
            fifo_put(&s->tx_fifo, (uint8_t) (value & 0xff));
            uart_transmit(NULL, G_IO_OUT, s);
        }
        break;

    case A_UART_INT_CLR:
        s->reg[R_UART_INT_ST] &= ~((uint32_t) value);
        s->reg[addr / 4] = value;
        break;

    case A_UART_INT_ENA:
        s->reg[addr / 4] = value;
        break;

    case A_UART_INT_RAW:
    case A_UART_INT_ST:
    case A_UART_STATUS:
        /* no-op */
        break;

    default:
        if (addr > sizeof(s->reg)) {
            error_report("esp_uart: write to addr=0x%x out of bounds\n", addr);
        } else {
            s->reg[addr / 4] = value;
        }
        break;

    }
    /* TODO: update IRQ status */
}


static gboolean uart_transmit(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    ESP32UARTState *s = ESP32_UART(opaque);

    /* Simple version using qemu_chr_fe_write_all.
     * FIXME: implement using async IO callbacks */
    uint8_t data[UART_FIFO_LENGTH];
    int count = fifo_count(&s->tx_fifo);
    for (int i = 0; i < count; ++i) {
        data[i] = fifo_get(&s->tx_fifo);
    }
    qemu_chr_fe_write_all(&s->chr, data, count);

    return FALSE;
}

static void uart_receive(void *opaque, const uint8_t *buf, int size)
{
    ESP32UARTState *s = ESP32_UART(opaque);

    if (size == 0) {
        return;
    }

    if (fifo_avail(&s->rx_fifo) == 0) {
        return;
    }

    for (int i = 0; i < size && fifo_avail(&s->rx_fifo) > 0; i++) {
        fifo_put(&s->rx_fifo, buf[i]);
    }

    /* TODO: update IRQs */
}

static int uart_can_receive(void *opaque)
{
    ESP32UARTState *s = ESP32_UART(opaque);

    return fifo_avail(&s->rx_fifo);
}

static void uart_event(void *opaque, int event)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    /* TODO: handle UART break */
}

static void esp32_uart_reset(DeviceState *dev)
{
    ESP32UARTState *s = ESP32_UART(dev);

    memset(s->reg, 0, sizeof(s->reg));
    fifo_reset(&s->tx_fifo);
    fifo_reset(&s->rx_fifo);
}


static void esp32_uart_realize(DeviceState *dev, Error **errp)
{
    ESP32UARTState *s = ESP32_UART(dev);

    qemu_chr_fe_init(&s->chr, serial_hd(0), &error_abort);
    qemu_chr_fe_set_handlers(&s->chr, uart_can_receive, uart_receive,
                             uart_event, NULL, s, NULL, true);
}


static const MemoryRegionOps uart_ops = {
    .read =  uart_read,
    .write = uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_uart_init(Object *obj)
{
    ESP32UARTState *s = ESP32_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s,
                          TYPE_ESP32_UART, UART_REG_CNT * sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static int esp32_uart_post_load(void *opaque, int version_id)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    /* update state after async load */
    return 0;
}

static Property esp32_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", ESP32UARTState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_uart_reset;
    dc->realize = esp32_uart_realize;
    dc->props = esp32_uart_properties;
}

static const TypeInfo esp32_uart_info = {
    .name = TYPE_ESP32_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32UARTState),
    .instance_init = esp32_uart_init,
    .class_init = esp32_uart_class_init
};

static void esp32_uart_register_types(void)
{
    type_register_static(&esp32_uart_info);
}

type_init(esp32_uart_register_types)

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
#include "hw/qdev-properties-system.h"
#include "hw/char/esp32_uart.h"
#include "trace.h"


static gboolean uart_transmit(void *do_not_use, GIOCondition cond, void *opaque);
static void uart_receive(void *opaque, const uint8_t *buf, int size);
static void uart_set_rx_timeout(ESP32UARTState *s);

static uint8_t fifo8_peek(Fifo8 *fifo)
{
    if (fifo->num == 0) {
        abort();
    }
    return fifo->data[fifo->head];
}

static void uart_update_irq(ESP32UARTState *s)
{
    bool irq = false;

    uint32_t tx_empty_threshold = FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, TXFIFO_EMPTY_THRD);
    uint32_t rx_full_threshold = FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, RXFIFO_FULL_THRD);

    uint32_t tx_empty_raw = (fifo8_num_used(&s->tx_fifo) <= tx_empty_threshold);
    uint32_t rx_full_raw = (fifo8_num_used(&s->rx_fifo) >= rx_full_threshold);
    uint32_t tx_done_raw = (fifo8_num_used(&s->tx_fifo) == 0);
    uint32_t rxfifo_tout_raw = (s->rxfifo_tout) ? 1 : 0;

    uint32_t int_raw = s->reg[R_UART_INT_RAW];
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, RXFIFO_FULL, rx_full_raw);
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, TXFIFO_EMPTY, tx_empty_raw);
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, TX_DONE, tx_done_raw);
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, RXFIFO_TOUT, rxfifo_tout_raw);
    s->reg[R_UART_INT_RAW] = int_raw;

    uint32_t int_st = s->reg[R_UART_INT_RAW] & s->reg[R_UART_INT_ENA];
    irq = int_st != 0;
    s->reg[R_UART_INT_ST] = int_st;

    qemu_set_irq(s->irq, irq);
}

static uint64_t uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    uint64_t r = 0;

    switch (addr) {
    case A_UART_FIFO:
        if (fifo8_num_used(&s->rx_fifo) == 0) {
            r = 0xEE;
            error_report("esp_uart: read UART FIFO while it is empty");
        } else {
            r = fifo8_pop(&s->rx_fifo);
            uart_update_irq(s);
            qemu_chr_fe_accept_input(&s->chr);
        }
        break;

    case A_UART_STATUS:
        r = FIELD_DP32(r, UART_STATUS, RXFIFO_CNT, fifo8_num_used(&s->rx_fifo));
        r = FIELD_DP32(r, UART_STATUS, TXFIFO_CNT, fifo8_num_used(&s->tx_fifo));
        break;

    case A_UART_LOWPULSE:
    case A_UART_HIGHPULSE:
        r = 337;  /* FIXME: this should depend on the APB frequency */
        break;
    case A_UART_MEM_CONF:
        r = FIELD_DP32(r, UART_MEM_CONF, RX_SIZE, (unsigned char)(UART_FIFO_LENGTH/128));
        r = FIELD_DP32(r, UART_MEM_CONF, TX_SIZE,  (unsigned char)(UART_FIFO_LENGTH/128));
        break;
    case A_UART_MEM_RX_STATUS: {
        uint32_t fifo_size = fifo8_num_used(&s->rx_fifo);
        /* The software only cares about the differene between WR_ADDR and RD_ADDR;
         * to keep things simpler, set RD_ADDR to 0 and WR_ADDR to the number of bytes
         * in the FIFO. 128 is a special case — write and read pointers should be
         * the same in this case.
         */
        r = FIELD_DP32(0, UART_MEM_RX_STATUS, WR_ADDR, (fifo_size == 128) ? 0 : fifo_size);
        }
        break;
    case A_UART_DATE:
        r = 0x15122500;
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
        if (fifo8_num_free(&s->tx_fifo) == 0) {
            error_report("esp_uart: write to UART FIFO while it is full");
        } else {
            fifo8_push(&s->tx_fifo, (uint8_t) (value & 0xff));
            uart_transmit(NULL, G_IO_OUT, s);
        }
        break;

    case A_UART_INT_CLR:
        s->reg[R_UART_INT_ST] &= ~((uint32_t) value);
        s->reg[addr / 4] = value;
        if (value & R_UART_INT_CLR_RXFIFO_TOUT_MASK) {
            s->rxfifo_tout = false;
        }
        break;

    case A_UART_INT_ENA:
        s->reg[addr / 4] = value;
        break;

    case A_UART_CLKDIV: {
        s->reg[addr / 4] = value;
        unsigned clkdiv = (FIELD_EX32(s->reg[R_UART_CLKDIV], UART_CLKDIV, CLKDIV) << 4) +
                          FIELD_EX32(s->reg[R_UART_CLKDIV], UART_CLKDIV, CLKDIV_FRAG);
        unsigned baud_rate = 115200;
        if (clkdiv != 0) {
            /* FIXME: this should depend on the APB frequency */
            baud_rate = (unsigned) ((40000000ULL << 4) / clkdiv);
        }
        s->baud_rate = baud_rate;
        break;
    }

    case A_UART_AUTOBAUD:
        /* If autobaud is enabled, pretend that sufficient number of edges on the RXD line
         * have been received instantly. Autobaud is only used in the ROM bootloader,
         * and it doesn't care if the result is ready immediately.
         */
        if (FIELD_EX32(value, UART_AUTOBAUD, EN)) {
            s->reg[R_UART_RXD_CNT] = 0x3FF;
        } else {
            s->reg[R_UART_RXD_CNT] = 0;
        }
        s->reg[addr / 4] = value;
        break;

    case A_UART_INT_RAW:
    case A_UART_INT_ST:
    case A_UART_STATUS:
        /* no-op */
        break;

    case A_UART_CONF1:
        s->reg[addr / 4] = value;
        uart_set_rx_timeout(s);
        uart_update_irq(s);
        break;

    default:
        if (addr > sizeof(s->reg)) {
            error_report("esp_uart: write to addr=0x%x out of bounds\n", (uint32_t) addr);
        } else {
            s->reg[addr / 4] = value;
        }
        break;

    }
    uart_update_irq(s);
}


static gboolean uart_transmit(void *do_not_use, GIOCondition cond, void *opaque)
{
    ESP32UARTState *s = ESP32_UART(opaque);

    s->tx_watch_handle = 0;

    /* drain the fifo instantly, if the char device backend is not connected */
    if (!qemu_chr_fe_backend_open(&s->chr)) {
        fifo8_reset(&s->tx_fifo);
        return FALSE;
    }

    while (fifo8_num_used(&s->tx_fifo) > 0) {
        uint8_t b = fifo8_peek(&s->tx_fifo);
        int r = qemu_chr_fe_write(&s->chr, &b, 1);
        if (r == 1) {
            fifo8_pop(&s->tx_fifo);
        } else {
            s->tx_watch_handle = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                                       uart_transmit, s);
            break;
        }
    }

    uart_update_irq(s);

    return FALSE;
}

static void uart_receive(void *opaque, const uint8_t *buf, int size)
{
    ESP32UARTState *s = ESP32_UART(opaque);

    if (size == 0) {
        return;
    }

    /* If we can receive anything: cancel any pending RX timeout timer,
     * and clear the receive timeout flag.
     */
    if (fifo8_num_free(&s->rx_fifo) > 0) {
        timer_del(&s->rx_timeout_timer);
        s->rxfifo_tout = false;
    }

    /* Move the data into the FIFO */
    for (int i = 0; i < size && fifo8_num_free(&s->rx_fifo) > 0; i++) {
        fifo8_push(&s->rx_fifo, buf[i]);
    }

    /* Receive throttling: some applications (in particular the ESP32 ROM bootloader)
     * may work incorrectly if the data comes in much faster than what UART baud rate
     * would allow. This code adds a delay every UART_FIFO_LENGTH bytes, to make the
     * average data rate match the configured baud rate.
     * This doesn't need to be very precise, so only add the delay if the FIFO is full
     * (which most likely means that more data will come).
     */
    if (fifo8_is_full(&s->rx_fifo)) {
        s->throttle_rx = true;
        const int bits_per_symbol = 10;
        int64_t throttle_time_ns = (int64_t) UART_FIFO_LENGTH * bits_per_symbol * NANOSECONDS_PER_SECOND / s->baud_rate;
        timer_mod_ns(&s->throttle_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     throttle_time_ns);
    }

    uart_set_rx_timeout(s);
    uart_update_irq(s);
}

static void uart_set_rx_timeout(ESP32UARTState *s)
{
    if (FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, TOUT_EN)) {
        unsigned threshold_reg = FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, TOUT_THRD);
        /* On the ESP32, threshold_reg is in units of (bit_time * 8).
         * Note this is different on later chips.
         */
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t rx_timeout_ns = now + threshold_reg * 8 * NANOSECONDS_PER_SECOND / s->baud_rate;
        /* If throttling is done, make sure timeout doesn't happen before more data
         * is allowed to come. Offset it by 1ms.
         */
        if (rx_timeout_ns <= s->throttle_timer.expire_time) {
            rx_timeout_ns = s->throttle_timer.expire_time + 10000000;
        }
        timer_mod_ns(&s->rx_timeout_timer, rx_timeout_ns);
    } else {
        timer_del(&s->rx_timeout_timer);
        s->rxfifo_tout = false;
    }
}

static int uart_can_receive(void *opaque)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    if (s->throttle_rx) {
        return 0;
    }
    return fifo8_num_free(&s->rx_fifo);
}

static void uart_event(void *opaque, QEMUChrEvent event)
{
    /* TODO: handle UART break */
}


static void uart_throttle_timer_cb(void* opaque)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    s->throttle_rx = false;
    qemu_chr_fe_accept_input(&s->chr);
}

static void uart_rx_timeout_timer_cb(void* opaque)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    s->rxfifo_tout = true;
    uart_update_irq(s);
}

static void esp32_uart_reset(DeviceState *dev)
{
    ESP32UARTState *s = ESP32_UART(dev);

    memset(s->reg, 0, sizeof(s->reg));
    s->reg[R_UART_RXD_CNT] = 0;
    s->reg[R_UART_INT_ST] = 0;
    s->reg[R_UART_INT_RAW] = 0;
    s->reg[R_UART_INT_ENA] = 0;
    s->reg[R_UART_AUTOBAUD] = 0;
    /* Default baud rate divider after reset */
    s->reg[R_UART_CLKDIV] = FIELD_DP32(0, UART_CLKDIV, CLKDIV, 0x2B6);
    s->baud_rate = 115200;
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);
    if (s->tx_watch_handle) {
        g_source_remove(s->tx_watch_handle);
        s->tx_watch_handle = 0;
    }
    timer_del(&s->throttle_timer);
    s->throttle_rx = false;
    qemu_irq_lower(s->irq);
}


static void esp32_uart_realize(DeviceState *dev, Error **errp)
{
    ESP32UARTState *s = ESP32_UART(dev);

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
    fifo8_create(&s->tx_fifo, UART_FIFO_LENGTH);
    fifo8_create(&s->rx_fifo, UART_FIFO_LENGTH);
    timer_init_ns(&s->throttle_timer, QEMU_CLOCK_VIRTUAL, uart_throttle_timer_cb, s);
    timer_init_ns(&s->rx_timeout_timer, QEMU_CLOCK_VIRTUAL, uart_rx_timeout_timer_cb, s);
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
    device_class_set_props(dc, esp32_uart_properties);
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

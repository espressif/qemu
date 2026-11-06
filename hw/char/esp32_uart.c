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


void esp32_uart_update_irq(ESP32UARTState *s)
{
    bool irq = false;

    uint32_t tx_empty_raw = (fifo8_num_used(&s->tx_fifo) <= s->tx_empty_threshold);
    uint32_t rx_full_raw = (s->rx_cnt >= s->rx_full_threshold);
    uint32_t tx_done_raw = (fifo8_num_used(&s->tx_fifo) == 0);
    uint32_t rxfifo_tout_raw = (s->rxfifo_tout) ? 1 : 0;
    uint32_t rxfifo_ovf_raw = (s->rxfifo_ovf) ? 1 : 0;

    uint32_t int_raw = s->reg[R_UART_INT_RAW];
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, RXFIFO_FULL, rx_full_raw);
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, TXFIFO_EMPTY, tx_empty_raw);
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, TX_DONE, tx_done_raw);
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, RXFIFO_TOUT, rxfifo_tout_raw);
    int_raw = FIELD_DP32(int_raw, UART_INT_RAW, RXFIFO_OVF, rxfifo_ovf_raw);
    s->reg[R_UART_INT_RAW] = int_raw;

    uint32_t int_st = s->reg[R_UART_INT_RAW] & s->reg[R_UART_INT_ENA];
    irq = int_st != 0;
    s->reg[R_UART_INT_ST] = int_st;

    qemu_set_irq(s->irq, irq);
}


void esp32_uart_set_rx_timeout(ESP32UARTState *s)
{
    if (s->rx_tout_ena) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t tout_ns = s->rx_tout_thres * NANOSECONDS_PER_SECOND / s->baud_rate;
        int64_t rx_timeout_ns = now + tout_ns;
        if (s->rx_overflow) {
            /* The line is idle once the delivered bytes would have finished arriving */
            rx_timeout_ns = MAX(now, s->throttle_timer.expire_time) + tout_ns;
        } else if (rx_timeout_ns <= s->throttle_timer.expire_time) {
            /* If throttling is done, make sure timeout doesn't happen before more data
             * is allowed to come. Offset it by 1ms.
             */
            rx_timeout_ns = s->throttle_timer.expire_time + 10000000;
        }
        timer_mod_ns(&s->rx_timeout_timer, rx_timeout_ns);
    } else {
        timer_del(&s->rx_timeout_timer);
        s->rxfifo_tout = false;
    }
}


static void esp32_uart_rx_fifo_reset(ESP32UARTState *s)
{
    s->rx_cnt = 0;
    s->rx_rd = 0;
    s->rx_wr = 0;
}


static uint64_t uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    uint64_t r = 0;

    switch (addr) {
    case A_UART_FIFO:
        if (s->rx_cnt == 0 && s->rx_rd == s->rx_wr) {
            r = 0xEE;
            error_report("esp_uart: read UART FIFO while it is empty");
        } else {
            /* After an overflow the pointers differ while the count is 0. Reads
             * still advance, which IDF's ESP32 FIFO reset relies on.
             */
            r = s->rx_mem[s->rx_rd];
            s->rx_rd = (s->rx_rd + 1) % UART_FIFO_LENGTH;
            if (s->rx_cnt > 0) {
                s->rx_cnt--;
            }
            esp32_uart_update_irq(s);
            qemu_chr_fe_accept_input(&s->chr);
        }
        break;

    case A_UART_STATUS:
        r = FIELD_DP32(r, UART_STATUS, RXFIFO_CNT, s->rx_cnt);
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
        /* The software only cares about the difference between WR_ADDR and RD_ADDR */
        r = FIELD_DP32(0, UART_MEM_RX_STATUS, WR_ADDR, s->rx_wr);
        r = FIELD_DP32(r, UART_MEM_RX_STATUS, RD_ADDR, s->rx_rd);
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
        if (value & R_UART_INT_CLR_RXFIFO_OVF_MASK) {
            s->rxfifo_ovf = false;
        }
        break;

    case A_UART_INT_ENA:
        s->reg[addr / 4] = value;
        break;

    case A_UART_CONF0:
        s->reg[addr / 4] = value;
        if (value & ESP32_UART_GET_CLASS(s)->rxfifo_rst_mask) {
            esp32_uart_rx_fifo_reset(s);
            timer_del(&s->rx_timeout_timer);
            qemu_chr_fe_accept_input(&s->chr);
        }
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
        s->tx_empty_threshold = FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, TXFIFO_EMPTY_THRD);
        s->rx_full_threshold = FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, RXFIFO_FULL_THRD);
        /* On the ESP32, rx_tout_thres is in units of (bit_time * 8).
         * Note this is different on later chips.
         */
        s->rx_tout_thres = 8 * FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, TOUT_THRD);
        s->rx_tout_ena = FIELD_EX32(s->reg[R_UART_CONF1], UART_CONF1, TOUT_EN) != 0;
        esp32_uart_set_rx_timeout(s);
        esp32_uart_update_irq(s);
        break;

    default:
        if (addr > sizeof(s->reg)) {
            error_report("esp_uart: write to addr=0x%x out of bounds\n", (uint32_t) addr);
        } else {
            s->reg[addr / 4] = value;
        }
        break;

    }
    esp32_uart_update_irq(s);
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

    esp32_uart_update_irq(s);

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
    if (s->rx_cnt < UART_FIFO_LENGTH) {
        timer_del(&s->rx_timeout_timer);
        s->rxfifo_tout = false;
    }

    /* Move the data into the FIFO. With rx-overflow, a byte that arrives while
     * the FIFO is full overwrites the oldest position, the count stays at the
     * FIFO length, and RXFIFO_OVF is raised, as measured on ESP32, ESP32-S3 and
     * ESP32-C3. The read and write pointers then stay out of step until a FIFO
     * reset, so later reads can return old bytes.
     */
    for (int i = 0; i < size; i++) {
        if (s->rx_cnt < UART_FIFO_LENGTH) {
            s->rx_cnt++;
        } else if (s->rx_overflow) {
            s->rxfifo_ovf = true;
        } else {
            break;
        }
        s->rx_mem[s->rx_wr] = buf[i];
        s->rx_wr = (s->rx_wr + 1) % UART_FIFO_LENGTH;
    }

    /* Receive throttling: some applications (in particular the ESP32 ROM bootloader)
     * may work incorrectly if the data comes in much faster than what UART baud rate
     * would allow. Without rx-overflow, delay every UART_FIFO_LENGTH bytes. With it,
     * take nothing more until these bytes would have finished arriving.
     */
    if (s->rx_overflow || s->rx_cnt == UART_FIFO_LENGTH) {
        s->throttle_rx = true;
        const int bits_per_symbol = 10;
        int symbols = s->rx_overflow ? size : UART_FIFO_LENGTH;
        int64_t throttle_time_ns = (int64_t) symbols * bits_per_symbol * NANOSECONDS_PER_SECOND / s->baud_rate;
        timer_mod_ns(&s->throttle_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     throttle_time_ns);
    }

    esp32_uart_set_rx_timeout(s);
    esp32_uart_update_irq(s);
}

static int uart_can_receive(void *opaque)
{
    ESP32UARTState *s = ESP32_UART(opaque);
    if (s->throttle_rx) {
        return 0;
    }
    /* With rx-overflow, a FIFO's worth of line time, whether or not there is room for it. */
    return s->rx_overflow ? UART_FIFO_LENGTH : UART_FIFO_LENGTH - s->rx_cnt;
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
    esp32_uart_update_irq(s);
}

static void esp32_uart_reset_hold(Object *obj, ResetType type)
{
    ESP32UARTState *s = ESP32_UART(obj);

    memset(s->reg, 0, sizeof(s->reg));
    s->reg[R_UART_RXD_CNT] = 0;
    s->reg[R_UART_INT_ST] = 0;
    s->reg[R_UART_INT_RAW] = 0;
    s->rxfifo_ovf = false;
    s->reg[R_UART_INT_ENA] = 0;
    s->reg[R_UART_AUTOBAUD] = 0;
    /* Default baud rate divider after reset */
    s->reg[R_UART_CLKDIV] = FIELD_DP32(0, UART_CLKDIV, CLKDIV, 0x2B6);
    s->baud_rate = 115200;
    fifo8_reset(&s->tx_fifo);
    esp32_uart_rx_fifo_reset(s);
    if (s->tx_watch_handle) {
        g_source_remove(s->tx_watch_handle);
        s->tx_watch_handle = 0;
    }
    timer_del(&s->throttle_timer);
    s->throttle_rx = false;
    s->rx_tout_ena = false;
    s->tx_empty_threshold = 0;
    s->rx_full_threshold = 0;
    s->rx_tout_thres = 0;
    qemu_irq_lower(s->irq);
}


static void esp32_uart_realize(DeviceState *dev, Error **errp)
{
    ESP32UARTState *s = ESP32_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, uart_can_receive, uart_receive,
                             uart_event, NULL, s, NULL, true);
}


static void esp32_uart_init(Object *obj)
{
    ESP32UARTState *s = ESP32_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ESP32UARTClass *class = ESP32_UART_GET_CLASS(obj);

    s->uart_ops = (MemoryRegionOps) {
        .read =  class->uart_read,
        .write = class->uart_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
    };

    memory_region_init_io(&s->iomem, obj, &s->uart_ops, s,
                          TYPE_ESP32_UART, UART_REG_CNT * sizeof(uint32_t));
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    fifo8_create(&s->tx_fifo, UART_FIFO_LENGTH);
    timer_init_ns(&s->throttle_timer, QEMU_CLOCK_VIRTUAL, uart_throttle_timer_cb, s);
    timer_init_ns(&s->rx_timeout_timer, QEMU_CLOCK_VIRTUAL, uart_rx_timeout_timer_cb, s);
}


static Property esp32_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", ESP32UARTState, chr),
    DEFINE_PROP_BOOL("rx-overflow", ESP32UARTState, rx_overflow, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32UARTClass *class = ESP32_UART_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    /* Populate the virtual attributes and methods here (if any) */
    class->uart_write = uart_write;
    class->uart_read = uart_read;
    class->rxfifo_rst_mask = R_UART_CONF0_RXFIFO_RST_MASK;

    rc->phases.hold = esp32_uart_reset_hold;
    dc->realize = esp32_uart_realize;
    device_class_set_props(dc, esp32_uart_properties);
}

static const TypeInfo esp32_uart_info = {
    .name = TYPE_ESP32_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32UARTState),
    .instance_init = esp32_uart_init,
    .class_init = esp32_uart_class_init,
    .class_size = sizeof(ESP32UARTClass)
};

static void esp32_uart_register_types(void)
{
    type_register_static(&esp32_uart_info);
}

type_init(esp32_uart_register_types)

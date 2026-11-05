/*
 * ESP32-H2 UART emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/char/esp32h2_uart.h"

/* Autobaud pulse width in UART clock cycles: 32 MHz XTAL at 115200 baud */
#define ESP32H2_UART_AUTOBAUD_PULSE 277

static uint64_t esp32h2_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32H2UARTClass *class = ESP32H2_UART_GET_CLASS(opaque);
    ESP32H2UARTState *s = ESP32H2_UART(opaque);

    switch (addr) {
    case A_ESP32H2_UART_RX_FILT:
    case A_ESP32H2_UART_HWFC_CONF:
    case A_ESP32H2_UART_SLEEP_CONF0:
    case A_ESP32H2_UART_MEM_CONF:
    case A_ESP32H2_UART_TOUT_CONF:
    case A_ESP32H2_UART_CLK_CONF:
        return s->regs[addr / sizeof(uint32_t)];

    case A_ESP32H2_UART_LOWPULSE:
    case A_ESP32H2_UART_HIGHPULSE:
        return s->parent.parent.reg[R_UART_RXD_CNT] ? ESP32H2_UART_AUTOBAUD_PULSE : 0xfff;

    case A_ESP32H2_UART_DATE:
        return 0x02207250;

    case A_ESP32H2_UART_AFIFO_STATUS:
        return 0xa;

    case A_ESP32H2_UART_ID:
        return 0x500;

    default:
        return class->parent_uart_read(opaque, addr, size);
    }
}

static void esp32h2_uart_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32H2UARTClass *class = ESP32H2_UART_GET_CLASS(opaque);
    ESP32H2UARTState *s = ESP32H2_UART(opaque);
    ESP32UARTState *base = &s->parent.parent;

    switch (addr) {
    case A_ESP32H2_UART_CONF1:
        s->parent.conf1 = value;
        base->tx_empty_threshold =
            FIELD_EX32(value, ESP32H2_UART_CONF1, TXFIFO_EMPTY_THRHD);
        base->rx_full_threshold =
            FIELD_EX32(value, ESP32H2_UART_CONF1, RXFIFO_FULL_THRHD);
        esp32_uart_set_rx_timeout(base);
        esp32_uart_update_irq(base);
        break;

    case A_ESP32H2_UART_TOUT_CONF:
        s->regs[R_ESP32H2_UART_TOUT_CONF] = value;
        base->rx_tout_ena =
            FIELD_EX32(value, ESP32H2_UART_TOUT_CONF, RX_TOUT_EN) != 0;
        base->rx_tout_thres =
            FIELD_EX32(value, ESP32H2_UART_TOUT_CONF, RX_TOUT_THRHD);
        esp32_uart_set_rx_timeout(base);
        esp32_uart_update_irq(base);
        break;

    case A_ESP32H2_UART_RX_FILT:
    case A_ESP32H2_UART_HWFC_CONF:
    case A_ESP32H2_UART_SLEEP_CONF0:
    case A_ESP32H2_UART_MEM_CONF:
    case A_ESP32H2_UART_CLK_CONF:
        s->regs[addr / sizeof(uint32_t)] = value;
        break;

    default:
        class->parent_uart_write(opaque, addr, value, size);
        break;
    }
}

static void esp32h2_uart_init(Object *obj)
{
    ESP32H2UARTState *s = ESP32H2_UART(obj);

    memory_region_set_size(&s->parent.parent.iomem, ESP32H2_UART_IO_SIZE);

    s->regs[R_ESP32H2_UART_TOUT_CONF] = 0x28;
    s->regs[R_ESP32H2_UART_CLK_CONF] = 0x03000000;
}

static void esp32h2_uart_class_init(ObjectClass *klass, void *data)
{
    ESP32H2UARTClass *h2 = ESP32H2_UART_CLASS(klass);
    ESP32UARTClass *esp32 = ESP32_UART_CLASS(klass);

    ESP32C3_UART_CLASS(klass)->rxd_cnt_reg = ESP32C6_UART_RXD_CNT_REG;

    h2->parent_uart_write = esp32->uart_write;
    h2->parent_uart_read = esp32->uart_read;
    esp32->uart_write = esp32h2_uart_write;
    esp32->uart_read = esp32h2_uart_read;
}

static const TypeInfo esp32h2_uart_info = {
    .name = TYPE_ESP32H2_UART,
    .parent = TYPE_ESP32C3_UART,
    .instance_size = sizeof(ESP32H2UARTState),
    .instance_init = esp32h2_uart_init,
    .class_init = esp32h2_uart_class_init,
    .class_size = sizeof(ESP32H2UARTClass),
};

static void esp32h2_uart_register_types(void)
{
    type_register_static(&esp32h2_uart_info);
}

type_init(esp32h2_uart_register_types)

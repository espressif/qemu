/*
 * ESP32-P4 UART
 *
 * The register block matches the ESP32-C3 up to CONF1 (0x24) and then diverges.
 * This override covers the divergences the model actually depends on; the
 * registers above UART_DATE (CLK_CONF at 0x88, REG_UPDATE at 0x98, ID at 0x9C)
 * still fall outside the shared MMIO window and land in the machine's
 * unimplemented catch-all, which is harmless because the model derives its
 * baud rate from CLKDIV and applies writes immediately rather than on
 * REG_UPDATE.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/char/esp32p4_uart.h"

static uint64_t esp32p4_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32P4UARTClass *class = ESP32P4_UART_GET_CLASS(opaque);
    ESP32P4UARTState *s = ESP32P4_UART(opaque);

    if (addr == A_ESP32P4_UART_TOUT_CONF) {
        return s->tout_conf;
    }

    return class->parent_uart_read(opaque, addr, size);
}

static void esp32p4_uart_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32P4UARTClass *class = ESP32P4_UART_GET_CLASS(opaque);
    ESP32C3UARTClass *c3_class = ESP32C3_UART_GET_CLASS(opaque);
    ESP32P4UARTState *s = ESP32P4_UART(opaque);
    ESP32UARTState *base = &s->parent.parent;

    switch (addr) {
    case A_ESP32P4_UART_CONF0:
        c3_class->parent_uart_write(opaque, A_UART_AUTOBAUD,
                                    FIELD_EX32(value, ESP32P4_UART_CONF0,
                                               AUTOBAUD_EN) ? 1 : 0,
                                    sizeof(uint32_t));
        c3_class->parent_uart_write(opaque, addr, value, size);
        break;

    case A_ESP32P4_UART_CONF1:
        s->parent.conf1 = value;
        base->tx_empty_threshold =
            FIELD_EX32(value, ESP32P4_UART_CONF1, TXFIFO_EMPTY_THRHD);
        base->rx_full_threshold =
            FIELD_EX32(value, ESP32P4_UART_CONF1, RXFIFO_FULL_THRHD);
        esp32_uart_set_rx_timeout(base);
        esp32_uart_update_irq(base);
        break;

    case A_ESP32P4_UART_TOUT_CONF:
        s->tout_conf = value;
        base->rx_tout_ena =
            FIELD_EX32(value, ESP32P4_UART_TOUT_CONF, RX_TOUT_EN) != 0;
        base->rx_tout_thres =
            FIELD_EX32(value, ESP32P4_UART_TOUT_CONF, RX_TOUT_THRHD);
        esp32_uart_set_rx_timeout(base);
        esp32_uart_update_irq(base);
        break;

    case A_ESP32P4_UART_MEM_CONF:
        /* Holds no RX timeout threshold on the P4, unlike the C3 */
        c3_class->parent_uart_write(opaque, addr, value, size);
        break;

    default:
        class->parent_uart_write(opaque, addr, value, size);
        break;
    }
}

static void esp32p4_uart_class_init(ObjectClass *klass, void *data)
{
    ESP32P4UARTClass *p4 = ESP32P4_UART_CLASS(klass);
    ESP32UARTClass *esp32 = ESP32_UART_CLASS(klass);

    p4->parent_uart_write = esp32->uart_write;
    p4->parent_uart_read = esp32->uart_read;
    esp32->uart_write = esp32p4_uart_write;
    esp32->uart_read = esp32p4_uart_read;
}

static const TypeInfo esp32p4_uart_info = {
    .name = TYPE_ESP32P4_UART,
    .parent = TYPE_ESP32C3_UART,
    .instance_size = sizeof(ESP32P4UARTState),
    .class_init = esp32p4_uart_class_init,
    .class_size = sizeof(ESP32P4UARTClass),
};

static void esp32p4_uart_register_types(void)
{
    type_register_static(&esp32p4_uart_info);
}

type_init(esp32p4_uart_register_types)

/*
 * ESP32C3 UART emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This implementation overrides the ESP32 UARt one, check it out first.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/char/esp32c3_uart.h"
#include "hw/misc/esp32c3_rtc_cntl.h"

#define ESP32_UART_AUTOBAUD A_UART_AUTOBAUD
#define ESP32_UART_CONF1    A_UART_CONF1

static uint64_t esp32c3_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3UARTClass *class = ESP32C3_UART_GET_CLASS(opaque);
    ESP32C3UARTState *s = ESP32C3_UART(opaque);
    uint32_t r = 0;

    switch (addr)
    {
    case A_ESP32C3_UART_CONF1:
        /* Return the mirrored conf1 */
        r = s->conf1;
        break;

    default:
        r = class->parent_uart_read(opaque, addr, size);
        break;
    }

    return r;
}

static void esp32c3_uart_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32C3UARTClass *class = ESP32C3_UART_GET_CLASS(opaque);
    ESP32C3UARTState *s = ESP32C3_UART(opaque);
    uint32_t autobaud = 0;

    /* UART_RXD_CNT_REG register is not at the same address on the ESP32 and ESP32-C3, so make the
     * the translation here. */
    switch (addr) {

        case A_ESP32C3_UART_CONF0:
            /* On the C3, AUTOBAUD is part of CONF0 register, but on the ESP32, it has its own
             * register, poke that register instead */
            autobaud = FIELD_EX32(value, ESP32C3_UART_CONF0, AUTOBAUD_EN) ? 1 : 0;
            class->parent_uart_write(opaque, ESP32_UART_AUTOBAUD, autobaud, sizeof(uint32_t));
            class->parent_uart_write(opaque, addr, value, size);
            break;

        case A_ESP32C3_UART_MEM_CONF:
            s->parent.rx_tout_thres = FIELD_EX32(value, ESP32C3_UART_MEM_CONF, RX_TOUT_THRHD);
            break;

        case A_ESP32C3_UART_CONF1:
            /* Store the value in our own mirror as the application may read it back */
            s->conf1 = value;

            /* Write the protected members of the parent's structure */
            s->parent.rx_tout_ena = FIELD_EX32(value, ESP32C3_UART_CONF1, RX_TOUT_EN);
            s->parent.tx_empty_threshold = FIELD_EX32(value, ESP32C3_UART_CONF1, TXFIFO_EMPTY_THRHD);
            s->parent.rx_full_threshold = FIELD_EX32(value, ESP32C3_UART_CONF1, RXFIFO_FULL_THRHD);

            esp32_uart_set_rx_timeout(&s->parent);
            esp32_uart_update_irq(&s->parent);
            break;

        default:
            class->parent_uart_write(opaque, addr, value, size);
            break;
    }
}

static void esp32c3_uart_reset(DeviceState *dev)
{
    /* Nothing special to do at the moment here, call the parent reset */
    ESP32C3UARTClass* esp32c3_class = ESP32C3_UART_GET_CLASS(dev);

    esp32c3_class->parent_reset(dev);
}

static void esp32c3_uart_realize(DeviceState *dev, Error **errp)
{
    // ESP32C3UARTState* esp32c3 = ESP32C3_UART(dev);
    ESP32C3UARTClass* esp32c3_class = ESP32C3_UART_GET_CLASS(dev);

    /* Call the realize function of the parent class: ESP32UARTClass */
    esp32c3_class->parent_realize(dev, errp);
}


static void esp32c3_uart_init(Object *obj)
{
    /* No need to call parent's class function, this is done automatically by the QOM, even before
     * calling the current function. */
}


static void esp32c3_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32C3UARTClass* esp32c3 = ESP32C3_UART_CLASS(klass);
    ESP32UARTClass* esp32 = ESP32_UART_CLASS(klass);

    /* Set our class' parent_realize field to the current realize function, set by the
     * parent class initializer.
     * After doing this, it will be necessary for our function, esp32c3_uart_realize,
     * to manually call the parent's one. */
    device_class_set_parent_realize(dc, esp32c3_uart_realize, &esp32c3->parent_realize);

    /* Let's do the same thing for the reset function */
    device_class_set_parent_reset(dc, esp32c3_uart_reset, &esp32c3->parent_reset);

    /* Override the UART operations functions */
    esp32c3->parent_uart_write = esp32->uart_write;
    esp32c3->parent_uart_read = esp32->uart_read;
    esp32->uart_write = esp32c3_uart_write;
    esp32->uart_read = esp32c3_uart_read;
}

static const TypeInfo esp32c3_uart_info = {
    .name = TYPE_ESP32C3_UART,
    .parent = TYPE_ESP32_UART,
    .instance_size = sizeof(ESP32C3UARTState),
    .instance_init = esp32c3_uart_init,
    .class_init = esp32c3_uart_class_init,
    .class_size = sizeof(ESP32C3UARTClass)
};

static void esp32c3_uart_register_types(void)
{
    type_register_static(&esp32c3_uart_info);
}

type_init(esp32c3_uart_register_types)

/*
 * ESP32S3 UART emulation
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
#include "hw/char/esp32s3_uart.h"
#include "hw/misc/esp32s3_rtc_cntl.h"

#define ESP32_UART_AUTOBAUD A_UART_AUTOBAUD
#define ESP32_UART_CONF1    A_UART_CONF1

static uint64_t esp32s3_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3UARTClass *class = ESP32S3_UART_GET_CLASS(opaque);
    ESP32S3UARTState *s = ESP32S3_UART(opaque);
    uint32_t r = 0;

    switch (addr)
    {
    case A_ESP32S3_UART_CONF1:
        /* Return the mirrored conf1 */
        r = s->conf1;
        break;

    default:
        r = class->parent_uart_read(opaque, addr, size);
        break;
    }

    return r;
}

static void esp32s3_uart_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32S3UARTClass *class = ESP32S3_UART_GET_CLASS(opaque);
    ESP32S3UARTState *s = ESP32S3_UART(opaque);
    uint32_t autobaud = 0;

    /* UART_RXD_CNT_REG register is not at the same address on the ESP32 and ESP32-S3, so make the
     * the translation here. */
    switch (addr) {

        case A_ESP32S3_UART_CONF0:
            /* On the S3, AUTOBAUD is part of CONF0 register, but on the ESP32, it has its own
             * register, poke that register instead */
            autobaud = FIELD_EX32(value, ESP32S3_UART_CONF0, AUTOBAUD_EN) ? 1 : 0;
            class->parent_uart_write(opaque, ESP32_UART_AUTOBAUD, autobaud, sizeof(uint32_t));
            class->parent_uart_write(opaque, addr, value, size);
            break;

        case A_ESP32S3_UART_MEM_CONF:
            s->parent.rx_tout_thres = FIELD_EX32(value, ESP32S3_UART_MEM_CONF, RX_TOUT_THRHD);
            break;

        case A_ESP32S3_UART_CONF1:
            /* Store the value in our own mirror as the application may read it back */
            s->conf1 = value;

            /* Write the protected members of the parent's structure */
            s->parent.rx_tout_ena = FIELD_EX32(value, ESP32S3_UART_CONF1, RX_TOUT_EN);
            s->parent.tx_empty_threshold = FIELD_EX32(value, ESP32S3_UART_CONF1, TXFIFO_EMPTY_THRHD);
            s->parent.rx_full_threshold = FIELD_EX32(value, ESP32S3_UART_CONF1, RXFIFO_FULL_THRHD);

            esp32_uart_set_rx_timeout(&s->parent);
            esp32_uart_update_irq(&s->parent);
            break;

        default:
            class->parent_uart_write(opaque, addr, value, size);
            break;
    }
}

static void esp32s3_uart_realize(DeviceState *dev, Error **errp)
{
    // ESP32S3UARTState* esp32s3 = ESP32S3_UART(dev);
    ESP32S3UARTClass* esp32s3_class = ESP32S3_UART_GET_CLASS(dev);

    /* Call the realize function of the parent class: ESP32UARTClass */
    esp32s3_class->parent_realize(dev, errp);
}


static void esp32s3_uart_init(Object *obj)
{
    /* No need to call parent's class function, this is done automatically by the QOM, even before
     * calling the current function. */
}


static void esp32s3_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32S3UARTClass* esp32s3 = ESP32S3_UART_CLASS(klass);
    ESP32UARTClass* esp32 = ESP32_UART_CLASS(klass);

    /* Set our class' parent_realize field to the current realize function, set by the
     * parent class initializer.
     * After doing this, it will be necessary for our function, esp32s3_uart_realize,
     * to manually call the parent's one. */
    device_class_set_parent_realize(dc, esp32s3_uart_realize, &esp32s3->parent_realize);

    /* Override the UART operations functions */
    esp32s3->parent_uart_write = esp32->uart_write;
    esp32s3->parent_uart_read = esp32->uart_read;
    esp32->uart_write = esp32s3_uart_write;
    esp32->uart_read = esp32s3_uart_read;
    /* The clock is chosen elsewhere, and CONF0's bit 27 is AUTOBAUD_EN */
    esp32->has_ref_tick = false;
}

static const TypeInfo esp32s3_uart_info = {
    .name = TYPE_ESP32S3_UART,
    .parent = TYPE_ESP32_UART,
    .instance_size = sizeof(ESP32S3UARTState),
    .instance_init = esp32s3_uart_init,
    .class_init = esp32s3_uart_class_init,
    .class_size = sizeof(ESP32S3UARTClass)
};

static void esp32s3_uart_register_types(void)
{
    type_register_static(&esp32s3_uart_info);
}

type_init(esp32s3_uart_register_types)

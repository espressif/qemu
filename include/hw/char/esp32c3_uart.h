#pragma once

#include "esp32_uart.h"


#define TYPE_ESP32C3_UART "esp32c3_soc.uart"
/* This macro can be used to "convert" a generic object into a more specific object, here ESP32C3UARTState.
 * For example, it can convert a DeviceState to a ESP32C3UARTState */
#define ESP32C3_UART(obj) OBJECT_CHECK(ESP32C3UARTState, (obj), TYPE_ESP32C3_UART)
/* This one can be used to get the class of a given object.
 * For example, it can give the ESP32UARTClass type structure out of a DeviceState or a
 * ESP32C3UARTState structure. */
#define ESP32C3_UART_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3UARTClass, obj, TYPE_ESP32C3_UART)
/* Finally, this macro is used to convert a generic class to ESP32C3UARTClass.
 * For example, it can be used to convert an ObjectClass to a ESP32C3UARTClass*/
#define ESP32C3_UART_CLASS(klass) OBJECT_CLASS_CHECK(ESP32C3UARTClass, klass, TYPE_ESP32C3_UART)

typedef struct ESP32C3UARTState {
    ESP32UARTState parent;

    uint32_t conf1;
} ESP32C3UARTState;

typedef struct ESP32C3UARTClass {
    ESP32UARTClass parent_class;

    /* These function pointers will be during class init, they will be populated
     * by device_class_set_parent_* functions. They can then be called in the
     * respective class methods: realize and reset. */
    DeviceRealize parent_realize;
    DeviceReset parent_reset;

    /* Virtual attributes/methods overriden */
    void (*parent_uart_write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
    uint64_t (*parent_uart_read)(void *opaque, hwaddr addr, unsigned int size);
} ESP32C3UARTClass;


/**
 * Define the ESP32-C3 specific registers, or the ones that saw their address or fields
 * changed from the ESP32 UART
 */
REG32(ESP32C3_UART_CONF0, 0x20)
    FIELD(ESP32C3_UART_CONF0, MEM_CLK_EN, 28, 1)
    FIELD(ESP32C3_UART_CONF0, AUTOBAUD_EN, 27, 1)
    FIELD(ESP32C3_UART_CONF0, ERR_WR_MASK, 26, 1)
    FIELD(ESP32C3_UART_CONF0, CLK_EN, 25, 1)
    FIELD(ESP32C3_UART_CONF0, DTR_INV, 24, 1)
    FIELD(ESP32C3_UART_CONF0, RTS_INV, 23, 1)
    FIELD(ESP32C3_UART_CONF0, TXD_INV, 22, 1)
    FIELD(ESP32C3_UART_CONF0, DSR_INV, 21, 1)
    FIELD(ESP32C3_UART_CONF0, CTS_INV, 20, 1)
    FIELD(ESP32C3_UART_CONF0, RXD_INV, 19, 1)
    FIELD(ESP32C3_UART_CONF0, TXFIFO_RST, 18, 1)
    FIELD(ESP32C3_UART_CONF0, RXFIFO_RST, 17, 1)
    FIELD(ESP32C3_UART_CONF0, IRDA_EN, 16, 1)
    FIELD(ESP32C3_UART_CONF0, TX_FLOW_EN, 15, 1)
    FIELD(ESP32C3_UART_CONF0, LOOPBACK, 14, 1)
    FIELD(ESP32C3_UART_CONF0, IRDA_RX_INV, 13, 1)
    FIELD(ESP32C3_UART_CONF0, IRDA_TX_INV, 12, 1)
    FIELD(ESP32C3_UART_CONF0, IRDA_WCTL, 11, 1)
    FIELD(ESP32C3_UART_CONF0, IRDA_TX_EN, 10, 1)
    FIELD(ESP32C3_UART_CONF0, IRDA_DPLX, 9, 1)
    FIELD(ESP32C3_UART_CONF0, TXD_BRK, 8, 1)
    FIELD(ESP32C3_UART_CONF0, SW_DTR, 7, 1)
    FIELD(ESP32C3_UART_CONF0, SW_RTS, 6, 1)
    FIELD(ESP32C3_UART_CONF0, STOP_BIT_NUM, 4, 2)
    FIELD(ESP32C3_UART_CONF0, BIT_NUM, 2, 2)
    FIELD(ESP32C3_UART_CONF0, PARITY_EN, 1, 1)
    FIELD(ESP32C3_UART_CONF0, PARITY, 0, 1)

REG32(ESP32C3_UART_CONF1, 0x24)
    FIELD(ESP32C3_UART_CONF1, RX_TOUT_EN, 21, 1)
    FIELD(ESP32C3_UART_CONF1, RX_FLOW_EN, 20, 1)
    FIELD(ESP32C3_UART_CONF1, RX_TOUT_FLOW_DIS, 19, 1)
    FIELD(ESP32C3_UART_CONF1, DIS_RX_DAT_OVF, 18, 1)
    FIELD(ESP32C3_UART_CONF1, TXFIFO_EMPTY_THRHD, 9, 9)
    FIELD(ESP32C3_UART_CONF1, RXFIFO_FULL_THRHD, 0, 9)

REG32(ESP32C3_UART_MEM_CONF, 0x60)
    FIELD(ESP32C3_UART_MEM_CONF, MEM_FORCE_PU, 27, 1)
    FIELD(ESP32C3_UART_MEM_CONF, MEM_FORCE_PD, 26, 1)
    FIELD(ESP32C3_UART_MEM_CONF, RX_TOUT_THRHD, 16, 10)
    FIELD(ESP32C3_UART_MEM_CONF, RX_FLOW_THRHD, 7, 9)
    FIELD(ESP32C3_UART_MEM_CONF, TX_SIZE, 4, 3)
    FIELD(ESP32C3_UART_MEM_CONF, RX_SIZE, 1, 3)

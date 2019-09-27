#pragma once

#include "qemu/fifo8.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/hw.h"
#include "hw/registerfields.h"

#define UART_FIFO_LENGTH 128

#define TYPE_ESP32_UART "esp_soc.uart"
#define ESP32_UART_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32UARTClass, obj, TYPE_ESP32_UART)
#define ESP32_UART_CLASS(klass) OBJECT_CLASS_CHECK(ESP32UARTClass, klass, TYPE_ESP32_UART)
#define ESP32_UART(obj) OBJECT_CHECK(ESP32UARTState, (obj), TYPE_ESP32_UART)

REG32(UART_FIFO, 0x0)
REG32(UART_INT_RAW, 0x4)
    FIELD(UART_INT_RAW, RXFIFO_FULL, 0, 1)
    FIELD(UART_INT_RAW, TXFIFO_EMPTY, 1, 1)
    FIELD(UART_INT_RAW, RXFIFO_OVF, 4, 1)
    FIELD(UART_INT_RAW, RXFIFO_TOUT, 8, 1)
    FIELD(UART_INT_RAW, TX_DONE, 14, 1)
REG32(UART_INT_ST, 0x8)
    FIELD(UART_INT_ST, RXFIFO_FULL, 0, 1)
    FIELD(UART_INT_ST, TXFIFO_EMPTY, 1, 1)
    FIELD(UART_INT_ST, RXFIFO_OVF, 4, 1)
    FIELD(UART_INT_ST, RXFIFO_TOUT, 8, 1)
    FIELD(UART_INT_ST, TX_DONE, 14, 1)
REG32(UART_INT_ENA, 0xC)
    FIELD(UART_INT_ENA, RXFIFO_FULL, 0, 1)
    FIELD(UART_INT_ENA, TXFIFO_EMPTY, 1, 1)
    FIELD(UART_INT_ENA, RXFIFO_OVF, 4, 1)
    FIELD(UART_INT_ENA, RXFIFO_TOUT, 8, 1)
    FIELD(UART_INT_ENA, TX_DONE, 14, 1)
REG32(UART_INT_CLR, 0x10)
    FIELD(UART_INT_CLR, RXFIFO_FULL, 0, 1)
    FIELD(UART_INT_CLR, TXFIFO_EMPTY, 1, 1)
    FIELD(UART_INT_CLR, RXFIFO_OVF, 4, 1)
    FIELD(UART_INT_CLR, RXFIFO_TOUT, 8, 1)
    FIELD(UART_INT_CLR, TX_DONE, 14, 1)

/* TODO: implement */
REG32(UART_CLKDIV, 0x14)
    FIELD(UART_CLKDIV, CLKDIV, 0, 20)
    FIELD(UART_CLKDIV, CLKDIV_FRAG, 20, 4)

REG32(UART_AUTOBAUD, 0x18)
    FIELD(UART_AUTOBAUD, EN, 0, 1)

REG32(UART_STATUS, 0x1C)
    FIELD(UART_STATUS, RXFIFO_CNT, 0, 8)
    FIELD(UART_STATUS, ST_URX_OUT, 8, 4)
    FIELD(UART_STATUS, TXFIFO_CNT, 16, 8)
    FIELD(UART_STATUS, ST_UTX_OUT, 24, 4)

REG32(UART_LOWPULSE, 0x28)
REG32(UART_HIGHPULSE, 0x2c)
REG32(UART_RXD_CNT, 0x30)

/* TODO: implement */
REG32(UART_CONF0, 0x20)
REG32(UART_CONF1, 0x24)
    FIELD(UART_CONF1, TOUT_EN, 31, 1)
    FIELD(UART_CONF1, TOUT_THRD, 24, 7)
    FIELD(UART_CONF1, TXFIFO_EMPTY_THRD, 8, 7)
    FIELD(UART_CONF1, RXFIFO_FULL_THRD, 0, 7)

REG32(UART_MEM_CONF, 0x58);
    FIELD(UART_MEM_CONF, RX_SIZE, 3, 4);
    FIELD(UART_MEM_CONF, TX_SIZE, 7, 4);
REG32(UART_MEM_RX_STATUS, 0x60);
    FIELD(UART_MEM_RX_STATUS, RD_ADDR, 2, 11);
    FIELD(UART_MEM_RX_STATUS, WR_ADDR, 13, 11);
REG32(UART_DATE, 0x78)

/* Size of the register file */
#define UART_REG_CNT (R_UART_DATE + 1)


typedef struct ESPUARTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;
    QEMUTimer throttle_timer;
    QEMUTimer rx_timeout_timer;
    bool throttle_rx;
    bool rxfifo_tout;
    unsigned baud_rate;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
    guint tx_watch_handle;

    uint32_t reg[UART_REG_CNT];
    MemoryRegionOps uart_ops;

    /* Protected: fields can be modified by the child class  */
    bool rx_tout_ena;
    /* Threshold, in bits, before triggering an RX timeout interrupt  */
    uint32_t rx_tout_thres;
    /* Threshold, in bytes, for a full RX FIFO and an empty TX FIFO respectively */
    uint32_t tx_empty_threshold;
    uint32_t rx_full_threshold;
} ESP32UARTState;

typedef struct ESPUARTClass {
    SysBusDeviceClass parent_class;

    /* Virtual attributes/methods */
    void (*uart_write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
    uint64_t (*uart_read)(void *opaque, hwaddr addr, unsigned int size);
} ESP32UARTClass;


/**
 * @brief Enable the RX timeout according to the protected members of ESP32UARTState, as such, their
 * values must be set and valid when calling this function.
 */
void esp32_uart_set_rx_timeout(ESP32UARTState *s);


/**
 * @brief Check the current state of the UART FIFOs and trigger an interrupt if enabled and if any reached
 * the configured threshold.
 */
void esp32_uart_update_irq(ESP32UARTState *s);

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

/* ESP8266 UART (see hw/char/esp8266_uart.c) */

#ifndef HW_CHAR_ESP8266_UART_H
#define HW_CHAR_ESP8266_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_ESP8266_UART "esp8266.uart"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266UartState, ESP8266_UART)

enum {
    ESP8266_UART_FIFO,
    ESP8266_UART_INT_RAW,
    ESP8266_UART_INT_ST,
    ESP8266_UART_INT_ENA,
    ESP8266_UART_INT_CLR,
    ESP8266_UART_CLKDIV,
    ESP8266_UART_AUTOBAUD,
    ESP8266_UART_STATUS,
    ESP8266_UART_CONF0,
    ESP8266_UART_CONF1,
    ESP8266_UART_LOWPULSE,
    ESP8266_UART_HIGHPULSE,
    ESP8266_UART_RXD_CNT,
    ESP8266_UART_DATE = 0x78 / 4,
    ESP8266_UART_ID,
    ESP8266_UART_MAX,
};

#define ESP8266_UART_FIFO_SIZE 0x80

struct Esp8266UartState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;

    uint32_t rx_first;
    uint32_t rx_last;
    uint8_t rx[ESP8266_UART_FIFO_SIZE];
    uint32_t reg[ESP8266_UART_MAX];
};

#endif

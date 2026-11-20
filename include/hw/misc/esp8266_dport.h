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

/* ESP8266 DPORT registers (see hw/misc/esp8266_dport.c) */

#ifndef HW_MISC_ESP8266_DPORT_H
#define HW_MISC_ESP8266_DPORT_H

#include "hw/sysbus.h"
#include "hw/clock.h"
#include "qom/object.h"

#define TYPE_ESP8266_DPORT "esp8266.dport"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266DportState, ESP8266_DPORT)

#define ESP8266_DPORT_NMI_FRC1 BIT(0)   /* 0x00: FRC1 routed to the NMI */
#define ESP8266_DPORT_CPU_CLK 0x14  /* byte offset; bit 0 = CPU2X */
#define ESP8266_DPORT_WIFI_EN 0x24  /* byte offset; bits 3/4, see dport_write */

enum {
    ESP8266_DPORT_SPI = 0x3,
    ESP8266_DPORT_MACADDR = 0x14,
    ESP8266_DPORT_MAX = 0x40,
};

/* Named GPIO outputs */
/* pulse: the WiFi block was stopped (0x24 bits 3/4 cleared) */
#define ESP8266_DPORT_WIFI_MAC_RST  "wifi-mac-rst"
/* level: FRC1 routed to the NMI (0x00 bit 0) */
#define ESP8266_DPORT_FRC_NMI_ROUTE "frc-nmi-route"

/* Output clock "cpu-clk": 80 MHz, or 160 MHz with CPU2X (0x14 bit 0). */
struct Esp8266DportState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *cpu_clk;
    qemu_irq wifi_rst;
    qemu_irq frc_nmi_route;

    uint32_t reg[ESP8266_DPORT_MAX];
};

#endif

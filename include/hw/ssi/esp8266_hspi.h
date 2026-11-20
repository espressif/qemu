/*
 * ESP8266 HSPI (SPI1), see hw/ssi/esp8266_hspi.c
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SSI_ESP8266_HSPI_H
#define HW_SSI_ESP8266_HSPI_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_ESP8266_HSPI "esp8266.hspi"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266HspiState, ESP8266_HSPI)

enum {
    ESP8266_HSPI_CMD = 0x00 / 4,
    ESP8266_HSPI_USER = 0x1c / 4,
    ESP8266_HSPI_USER1 = 0x20 / 4,
    ESP8266_HSPI_W0 = 0x40 / 4,
    ESP8266_HSPI_REGS = 0x100 / 4,
};

/* An SSI bus "spi" and the hardware chip select (SSI_GPIO_CS, active low). */
struct Esp8266HspiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SSIBus *bus;
    qemu_irq cs;
    qemu_irq irq;
    uint32_t reg[ESP8266_HSPI_REGS];
};

#endif

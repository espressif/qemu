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

/* ESP8266 SPI0 flash controller (see hw/ssi/esp8266_spi.c) */

#ifndef HW_SSI_ESP8266_SPI_H
#define HW_SSI_ESP8266_SPI_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ESP8266_SPI "esp8266.spi"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266SpiState, ESP8266_SPI)

enum {
    ESP8266_SPI_FLASH_CMD,
    ESP8266_SPI_FLASH_ADDR,
    ESP8266_SPI_FLASH_CTRL,
    ESP8266_SPI_FLASH_CTRL1,
    ESP8266_SPI_FLASH_STATUS,
    ESP8266_SPI_FLASH_CTRL2,
    ESP8266_SPI_FLASH_CLOCK,
    ESP8266_SPI_FLASH_USER,
    ESP8266_SPI_FLASH_USER1,
    ESP8266_SPI_FLASH_USER2,
    ESP8266_SPI_FLASH_USER3,
    ESP8266_SPI_FLASH_PIN,
    ESP8266_SPI_FLASH_SLAVE,
    ESP8266_SPI_FLASH_SLAVE1,
    ESP8266_SPI_FLASH_SLAVE2,
    ESP8266_SPI_FLASH_SLAVE3,
    ESP8266_SPI_FLASH_C0,
    ESP8266_SPI_FLASH_C1,
    ESP8266_SPI_FLASH_C2,
    ESP8266_SPI_FLASH_C3,
    ESP8266_SPI_FLASH_C4,
    ESP8266_SPI_FLASH_C5,
    ESP8266_SPI_FLASH_C6,
    ESP8266_SPI_FLASH_C7,

    ESP8266_SPI_FLASH_EXT0 = 0x3c,
    ESP8266_SPI_FLASH_EXT1,
    ESP8266_SPI_FLASH_EXT2,
    ESP8266_SPI_FLASH_EXT3,
    ESP8266_SPI_MAX,
};

#define ESP8266_SPI_FLASH_MAX   (16 * MiB)

/*
 * sysbus MMIO: 0 the registers, 1 the flash window (16 MiB, enabled by
 * CTRL.ENABLE_AHB). "drive": the flash image (-drive if=mtd), written
 * through when writable.
 */
struct Esp8266SpiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion cache;
    qemu_irq irq;
    BlockBackend *blk;
    bool writable;
    void *flash_image;
    uint32_t flash_size;

    uint32_t reg[ESP8266_SPI_MAX];
};

#endif

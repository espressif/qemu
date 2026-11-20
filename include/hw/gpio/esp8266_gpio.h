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

/*
 * ESP8266 GPIO (see hw/gpio/esp8266_gpio.c): 16 qdev GPIO inputs (external
 * pin levels) and 16 outputs (driven levels).
 */

#ifndef HW_GPIO_ESP8266_GPIO_H
#define HW_GPIO_ESP8266_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ESP8266_GPIO "esp8266.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266GpioState, ESP8266_GPIO)

#define ESP8266_GPIO_PINS 16

struct Esp8266GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;                       /* INUM 4, level */
    qemu_irq output[ESP8266_GPIO_PINS]; /* driven pin levels */
    uint32_t out;
    uint32_t enable;
    uint32_t pins;                      /* external levels of the input pins */
    uint32_t status;
    uint32_t conf[ESP8266_GPIO_PINS];
};

#endif

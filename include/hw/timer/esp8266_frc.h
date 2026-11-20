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

/* ESP8266 FRC1/FRC2 timers (see hw/timer/esp8266_frc.c) */

#ifndef HW_TIMER_ESP8266_FRC_H
#define HW_TIMER_ESP8266_FRC_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_ESP8266_FRC "esp8266.frc"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266FrcState, ESP8266_FRC)

/* The timers and the peripherals count with the 80 MHz APB clock. */
#define ESP8266_APB_HZ 80000000

/* Level input: FRC1 is routed to the NMI (DPORT 0x00 bit 0). */
#define ESP8266_FRC_NMI_ROUTE_GPIO "nmi-route"

/* sysbus IRQs: 0 FRC1 (INUM 9), 1 FRC2 (INUM 10), 2 the NMI. */
struct Esp8266FrcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq[2];
    qemu_irq nmi;
    bool nmi_route;
    QEMUTimer *timer[2];
    uint32_t load[2];
    uint32_t ctrl[2];
    uint32_t alarm;             /* FRC2 */
    uint32_t base_count[2];     /* counter value at base_ns */
    int64_t base_ns[2];
};

#endif

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

/* ESP8266 RTC and RTC memory (see hw/misc/esp8266_rtc.c) */

#ifndef HW_MISC_ESP8266_RTC_H
#define HW_MISC_ESP8266_RTC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ESP8266_RTC "esp8266.rtc"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266RtcState, ESP8266_RTC)

#define ESP8266_RTC_CTRL_SW_SYS_RST BIT(31)
#define ESP8266_RTC_RESET_POWER_ON  1
#define ESP8266_RTC_RESET_SW        2
#define ESP8266_RTC_MEM_SIZE        0x300

/* sysbus MMIO: 0 the registers, 1 the RTC memory (RAM, kept across resets) */
struct Esp8266RtcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion mem;
    uint32_t reset_cause;       /* survives the reset it describes */
};

#endif

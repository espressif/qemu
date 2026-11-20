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

/* ESP8266/ESP8285 SoC (see hw/xtensa/esp8266.c) */

#ifndef HW_XTENSA_ESP8266_H
#define HW_XTENSA_ESP8266_H

#include "hw/char/esp8266_uart.h"
#include "hw/gpio/esp8266_gpio.h"
#include "hw/misc/esp8266_ana.h"
#include "hw/misc/esp8266_dport.h"
#include "hw/misc/esp8266_iomux.h"
#include "hw/misc/esp8266_rng.h"
#include "hw/misc/esp8266_rtc.h"
#include "hw/misc/esp8266_sar.h"
#include "hw/net/esp8266_wifi.h"
#include "hw/ssi/esp8266_hspi.h"
#include "hw/ssi/esp8266_spi.h"
#include "hw/timer/esp8266_frc.h"
#include "hw/boards.h"
#include "target/xtensa/cpu.h"
#include "qom/object.h"

#define TYPE_ESP8266_SOC "esp8266.soc"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266SocState, ESP8266_SOC)

/*
 * The lx106 core and the on-chip peripherals. Properties: "cpu-type",
 * aliases "serial0" (UART0 chardev), "drive" (flash image) and "adc" (SAR
 * input). Boards connect external parts to the GPIO lines and the HSPI bus.
 */
struct Esp8266SocState {
    DeviceState parent_obj;

    char *cpu_type;
    XtensaCPU cpu;
    MemoryRegion dram;
    MemoryRegion io;            /* 0x60000000 peripheral window */

    Esp8266DportState dport;
    Esp8266UartState uart;
    Esp8266SpiState spi;
    Esp8266HspiState hspi;
    Esp8266GpioState gpio;
    Esp8266FrcState frc;
    Esp8266RtcState rtc;
    Esp8266IomuxState iomux;
    Esp8266AnaState ana;
    Esp8266SarState sar;
    Esp8266RngState rng;
    Esp8266WifiState wifi;
};

/* The esp8266 machine; boards derive from it and add external parts. */
#define TYPE_ESP8266_MACHINE MACHINE_TYPE_NAME("esp8266")
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266MachineState, ESP8266_MACHINE)

struct Esp8266MachineState {
    MachineState parent_obj;

    Esp8266SocState soc;
};

/* Creates the SoC and loads the mask ROM and the flash image. */
void esp8266_machine_init(MachineState *machine);

#endif

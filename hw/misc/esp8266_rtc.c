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
 * ESP8266 RTC (0x60000700) and RTC memory (0x60001000), based on Max
 * Filippov's ESP8266 model from OSLL/qemu-xtensa (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "sysemu/runstate.h"
#include "hw/misc/esp8266_rtc.h"

static uint64_t esp8266_rtc_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    Esp8266RtcState *s = opaque;

    switch (addr) {
    case 0x08: /*?*/
        return 0;

    case 0x14: /* reset reason, bits 0..3 */
        return s->reset_cause;

    case 0x18: /*?*/
        return 5;

    case 0x28: /*?*/
        return 1;

    default:
        qemu_log_mask(LOG_UNIMP, "%s, +0x%x\n", __func__, (uint32_t)addr);
        break;
    }
    return 0;
}

static void esp8266_rtc_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    Esp8266RtcState *s = opaque;

    qemu_log_mask(LOG_UNIMP, "%s: +0x%02x: %08x\n",
              __func__, (uint32_t)addr, (uint32_t)val);
    /* ROM software_reset() sets RTC_CTRL bit 31: system reset, cause 2. */
    if (addr == 0x00 && (val & ESP8266_RTC_CTRL_SW_SYS_RST)) {
        s->reset_cause = ESP8266_RTC_RESET_SW;
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static const MemoryRegionOps esp8266_rtc_ops = {
    .read = esp8266_rtc_read,
    .write = esp8266_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_rtc_realize(DeviceState *dev, Error **errp)
{
    Esp8266RtcState *s = ESP8266_RTC(dev);

    /*
     * RTC memory (0x60001000, 768 bytes: 256 for the SDK's reset info, 512
     * for rtcUserMemory). Not cleared on reset, so it carries data across
     * software restarts as on the chip.
     */
    memory_region_init_ram(&s->mem, OBJECT(dev), "esp8266.rtc-mem",
                           ESP8266_RTC_MEM_SIZE, errp);
}

static void esp8266_rtc_init(Object *obj)
{
    Esp8266RtcState *s = ESP8266_RTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Set at power-on only: it describes the reset that just happened. */
    s->reset_cause = ESP8266_RTC_RESET_POWER_ON;
    memory_region_init_io(&s->iomem, obj, &esp8266_rtc_ops, s,
                          TYPE_ESP8266_RTC, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_mmio(sbd, &s->mem);
}

static const VMStateDescription vmstate_esp8266_rtc = {
    .name = TYPE_ESP8266_RTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(reset_cause, Esp8266RtcState),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp8266_rtc_realize;
    dc->vmsd = &vmstate_esp8266_rtc;
}

static const TypeInfo esp8266_rtc_info = {
    .name = TYPE_ESP8266_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266RtcState),
    .instance_init = esp8266_rtc_init,
    .class_init = esp8266_rtc_class_init,
};

static void esp8266_rtc_register_types(void)
{
    type_register_static(&esp8266_rtc_info);
}

type_init(esp8266_rtc_register_types)

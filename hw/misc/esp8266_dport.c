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
 * ESP8266 DPORT registers (0x3ff00000), based on Max Filippov's ESP8266 model
 * from OSLL/qemu-xtensa (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/clock.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "hw/timer/esp8266_frc.h"
#include "hw/misc/esp8266_dport.h"

static uint64_t esp8266_dport_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    Esp8266DportState *s = opaque;

    qemu_log_mask(LOG_UNIMP, "%s: +0x%02x: 0x%08x\n",
              __func__, (uint32_t)addr % (ESP8266_DPORT_MAX * 4),
              s->reg[(addr / 4) % ESP8266_DPORT_MAX]);
    return s->reg[(addr / 4) % ESP8266_DPORT_MAX];
}

static void esp8266_dport_write_spi(Esp8266DportState *s, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    s->reg[ESP8266_DPORT_SPI] = val;
    if (val & 1) {
        s->reg[ESP8266_DPORT_SPI] |= 2;
    }
}

static void esp8266_dport_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    Esp8266DportState *s = opaque;
    static void (* const handler[])(Esp8266DportState *s, hwaddr addr,
                                    uint64_t val, unsigned size) = {
        [ESP8266_DPORT_SPI] = esp8266_dport_write_spi,
    };

    qemu_log_mask(LOG_UNIMP, "%s: +0x%02x = 0x%08x\n",
              __func__, (uint32_t)addr % (ESP8266_DPORT_MAX * 4),
              (uint32_t)val);
    if (addr % 4 || size != 4) {
        return;
    }
    /*
     * The SDK sets 0x24 bits 3/4 when it starts WiFi and clears them in
     * system_restart_core right before jumping to the ROM reset vector (and
     * before deep sleep). No hardware reset follows, so this is where the
     * MAC must stop its DMA. The register's name is not known.
     */
    if (addr == ESP8266_DPORT_WIFI_EN && (s->reg[addr / 4] & ~val & 0x18)) {
        qemu_irq_pulse(s->wifi_rst);
    }
    bool cpu2x_changed = addr == ESP8266_DPORT_CPU_CLK &&
                         ((s->reg[addr / 4] ^ val) & 1);

    if (addr / 4 < ARRAY_SIZE(handler) && handler[addr / 4]) {
        handler[addr / 4](s, addr, val, size);
    } else {
        s->reg[(addr / 4) % ESP8266_DPORT_MAX] = val;
    }
    if ((addr / 4) % ESP8266_DPORT_MAX == 0) {
        qemu_set_irq(s->frc_nmi_route, s->reg[0] & ESP8266_DPORT_NMI_FRC1);
    }
    if (cpu2x_changed) {
        /* Arduino sets CPU2X at boot for F_CPU = 160 MHz; APB stays 80 MHz. */
        clock_update_hz(s->cpu_clk,
                        (val & 1) ? 2 * ESP8266_APB_HZ : ESP8266_APB_HZ);
    }
}

static const MemoryRegionOps esp8266_dport_ops = {
    .read = esp8266_dport_read,
    .write = esp8266_dport_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_dport_reset_hold(Object *obj, ResetType type)
{
    Esp8266DportState *s = ESP8266_DPORT(obj);

    memset(s->reg, 0, sizeof(s->reg));
    s->reg[ESP8266_DPORT_MACADDR + 0] = 0x01234567;
    s->reg[ESP8266_DPORT_MACADDR + 1] = 0x89abcdef;
    s->reg[ESP8266_DPORT_MACADDR + 2] = 0x00008000;
}

static void esp8266_dport_reset_exit(Object *obj, ResetType type)
{
    qemu_set_irq(ESP8266_DPORT(obj)->frc_nmi_route, 0);
}

static void esp8266_dport_init(Object *obj)
{
    Esp8266DportState *s = ESP8266_DPORT(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_dport_ops, s,
                          TYPE_ESP8266_DPORT, 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_out_named(dev, &s->wifi_rst, ESP8266_DPORT_WIFI_MAC_RST, 1);
    qdev_init_gpio_out_named(dev, &s->frc_nmi_route,
                             ESP8266_DPORT_FRC_NMI_ROUTE, 1);
    /* CCOUNT runs at 80 MHz after power-on, as on the chip. */
    s->cpu_clk = qdev_init_clock_out(dev, "cpu-clk");
    clock_set_hz(s->cpu_clk, ESP8266_APB_HZ);
}

static const VMStateDescription vmstate_esp8266_dport = {
    .name = TYPE_ESP8266_DPORT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, Esp8266DportState, ESP8266_DPORT_MAX),
        VMSTATE_CLOCK(cpu_clk, Esp8266DportState),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_dport_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_esp8266_dport;
    rc->phases.hold = esp8266_dport_reset_hold;
    rc->phases.exit = esp8266_dport_reset_exit;
}

static const TypeInfo esp8266_dport_info = {
    .name = TYPE_ESP8266_DPORT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266DportState),
    .instance_init = esp8266_dport_init,
    .class_init = esp8266_dport_class_init,
};

static void esp8266_dport_register_types(void)
{
    type_register_static(&esp8266_dport_info);
}

type_init(esp8266_dport_register_types)

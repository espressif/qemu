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
 * ESP8266 pin multiplexer (IO_MUX, 0x60000800), based on Max Filippov's
 * ESP8266 model from OSLL/qemu-xtensa (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/misc/esp8266_iomux.h"

enum {
    DUMMY0,

    MTDI,
    MTCK,
    MTMS,
    MTDO,
    U0RXD,
    U0TXD,
    SD_CLK,
    SD_DATA0,
    SD_DATA1,
    SD_DATA2,
    SD_DATA3,
    SD_CMD,
    GPIO0,
    GPIO2,
    GPIO4,
    GPIO5,

    ESP8266_PINMUX_MAX,
};

QEMU_BUILD_BUG_ON(ESP8266_PINMUX_MAX != ESP8266_IOMUX_REGS);

static uint64_t esp8266_iomux_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    Esp8266IomuxState *s = opaque;

    if (addr % 4 == 0 && size == 4 && addr / 4 < ESP8266_IOMUX_REGS) {
        return s->reg[addr / 4];
    }
    return 0;
}

static unsigned esp8266_iomux_function(uint32_t v)
{
    return ((v & 0x30) >> 4) | ((v & 0x100) >> 6);
}

static unsigned esp8266_iomux_pullup(uint32_t v)
{
    return (v & 0x80) >> 7;
}

static unsigned esp8266_iomux_pulldn(uint32_t v)
{
    return (v & 0x40) >> 6;
}

static unsigned esp8266_iomux_oe(uint32_t v)
{
    return v & 0x1;
}

static void esp8266_iomux_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    static const struct {
        const char * const fn[5];
    } pin[] = {
        {},
        { .fn = { "MTDI", "I2SI_DATA", "HSPIQ", "GPIO12", "U0DTR" } },
        { .fn = { "MTCK", "I2SI_BCK", "HSPID", "GPIO13", "U0CTS" } },
        { .fn = { "MTMS", "I2SI_WS", "HSPICLK", "GPIO14", "U0DSR" } },
        { .fn = { "MTDO", "I2SO_BCK", "HSPICS", "GPIO15", "U0RTS" } },
        { .fn = { "U0RXD", "I2SO_DATA", "", "GPIO3", "CLK_XTAL" } },
        { .fn = { "U0TXD", "SPICS1", "", "GPIO1", "CLK_RTC" } },
        { .fn = { "SD_CLK", "SPI_CLK", "", "GPIO6", "U1CTS" } },
        { .fn = { "SD_DATA0", "SPIQ", "", "GPIO7", "U1TXD" } },
        { .fn = { "SD_DATA1", "SPID", "", "GPIO8", "U1RXD" } },
        { .fn = { "SD_DATA2", "SPIHD", "", "GPIO9", "HSPIHD" } },
        { .fn = { "SD_DATA3", "SPIWP", "", "GPIO10", "HSPIWP" } },
        { .fn = { "SD_CMD", "SPICS0", "", "GPIO11", "U1RTS" } },
        { .fn = { "GPIO0", "SPICS2", "", "", "CLK_OUT" } },
        { .fn = { "GPIO2", "I2SOWS", "U1TXD", "", "U0TXD" } },
        { .fn = { "GPIO4", "CLK_XTAL", "", "", "" } },
        { .fn = { "GPIO5", "CLK_RTC", "", "", "" } },
    };
    Esp8266IomuxState *s = opaque;
    unsigned i = addr / 4;

    if (addr < 4 || addr / 4 >= ESP8266_IOMUX_REGS || addr % 4 || size != 4) {
        return;
    }

    if ((s->reg[i] ^ val) & 0x1f1) {
        qemu_log_mask(LOG_UNIMP, "%s: ", pin[i].fn[0]);
        if (esp8266_iomux_function(s->reg[i]) != esp8266_iomux_function(val)) {
            qemu_log_mask(LOG_UNIMP, "[%s -> %s]",
                pin[i].fn[esp8266_iomux_function(s->reg[i])],
                pin[i].fn[esp8266_iomux_function(val)]);
        }
        if (esp8266_iomux_oe(s->reg[i]) != esp8266_iomux_oe(val)) {
            qemu_log_mask(LOG_UNIMP, "[oe: %d -> %d]",
                    esp8266_iomux_oe(s->reg[i]),
                    esp8266_iomux_oe(val));
        }
        if (esp8266_iomux_pullup(s->reg[i]) != esp8266_iomux_pullup(val)) {
            qemu_log_mask(LOG_UNIMP, "[pullup: %d -> %d]",
                    esp8266_iomux_pullup(s->reg[i]),
                    esp8266_iomux_pullup(val));
        }
        if (esp8266_iomux_pulldn(s->reg[i]) != esp8266_iomux_pulldn(val)) {
            qemu_log_mask(LOG_UNIMP, "[pulldn: %d -> %d]",
                    esp8266_iomux_pulldn(s->reg[i]),
                    esp8266_iomux_pulldn(val));
        }
        qemu_log_mask(LOG_UNIMP, "\n");
    }

    s->reg[i] = val;
}

static const MemoryRegionOps esp8266_iomux_ops = {
    .read = esp8266_iomux_read,
    .write = esp8266_iomux_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_iomux_reset_hold(Object *obj, ResetType type)
{
    Esp8266IomuxState *s = ESP8266_IOMUX(obj);
    static const uint32_t reset_reg[] = {
        0x00000080,
        0x00000080,
        0x00000080,
        0x00000080,
        0x00000080,
        0x00000000,
        0x00000010,
        0x00000010,
        0x00000010,
        0x00000010,
        0x00000010,
        0x00000010,
        0x00000080,
        0x000000a0,
        0x00000000,
        0x00000000,
    };

    /* reset_reg has one entry fewer than reg; the last register resets to 0. */
    memset(s->reg, 0, sizeof(s->reg));
    memcpy(s->reg, reset_reg, sizeof(reset_reg));
}

static void esp8266_iomux_init(Object *obj)
{
    Esp8266IomuxState *s = ESP8266_IOMUX(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_iomux_ops, s,
                          TYPE_ESP8266_IOMUX, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_esp8266_iomux = {
    .name = TYPE_ESP8266_IOMUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, Esp8266IomuxState, ESP8266_IOMUX_REGS),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_iomux_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_esp8266_iomux;
    rc->phases.hold = esp8266_iomux_reset_hold;
}

static const TypeInfo esp8266_iomux_info = {
    .name = TYPE_ESP8266_IOMUX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266IomuxState),
    .instance_init = esp8266_iomux_init,
    .class_init = esp8266_iomux_class_init,
};

static void esp8266_iomux_register_types(void)
{
    type_register_static(&esp8266_iomux_info);
}

type_init(esp8266_iomux_register_types)

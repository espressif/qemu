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
 * ESP8266 internal analog I2C master (0x60000d00) with the PLL device, split
 * out of the ESP8266 machine (hw/xtensa/esp8266.c); the model is Max
 * Filippov's, from OSLL/qemu-xtensa (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "hw/misc/esp8266_ana.h"

/*
 * Each 32-bit register is one bus of the SDK's rom_i2c_readReg/writeReg:
 * a write with WRITE set writes DATA to register REG of device ADDR; a write
 * without it reads that register back into DATA. Only the PLL (bus 1,
 * address 0x62) answers; its registers read as at reset and ignore writes.
 * Other addresses read as 0xff.
 */
FIELD(ANA_CMD, ADDR, 0, 7)
FIELD(ANA_CMD, REG, 8, 8)
FIELD(ANA_CMD, DATA, 16, 8)
FIELD(ANA_CMD, WRITE, 24, 1)
FIELD(ANA_CMD, BUSY, 25, 1)

#define ESP8266_ANA_PLL_BUS     1
#define ESP8266_ANA_PLL_ADDR    0x62

static uint8_t esp8266_ana_pll_read(Esp8266AnaState *s, uint8_t reg)
{
    return reg < ESP8266_ANA_PLL_REGS ? s->pll[reg] : 0;
}

static uint64_t esp8266_ana_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp8266AnaState *s = opaque;

    qemu_log_mask(LOG_UNIMP, "%s: +0x%02x: 0x%08x\n", __func__,
                  (uint32_t)addr, s->reg[addr / 4]);
    return s->reg[addr / 4];
}

static void esp8266_ana_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    Esp8266AnaState *s = opaque;
    unsigned bus = addr / 4;
    bool pll = bus == ESP8266_ANA_PLL_BUS &&
               FIELD_EX32(val, ANA_CMD, ADDR) == ESP8266_ANA_PLL_ADDR;
    uint8_t data;

    qemu_log_mask(LOG_UNIMP, "%s: +0x%02x = 0x%08x\n", __func__,
                  (uint32_t)addr, (uint32_t)val);
    if (addr % 4 || size != 4 || FIELD_EX32(val, ANA_CMD, WRITE)) {
        return;     /* the PLL ignores writes; nothing else is there */
    }
    data = pll ? esp8266_ana_pll_read(s, FIELD_EX32(val, ANA_CMD, REG)) : 0xff;
    s->reg[bus] = val & (R_ANA_CMD_ADDR_MASK | R_ANA_CMD_REG_MASK);
    s->reg[bus] = FIELD_DP32(s->reg[bus], ANA_CMD, DATA, data);
}

static const MemoryRegionOps esp8266_ana_ops = {
    .read = esp8266_ana_read,
    .write = esp8266_ana_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_ana_reset_hold(Object *obj, ResetType type)
{
    Esp8266AnaState *s = ESP8266_ANA(obj);

    memset(s->reg, 0, sizeof(s->reg));
    memset(s->pll, 0, sizeof(s->pll));
    s->pll[7] = 0xff;
}

static void esp8266_ana_init(Object *obj)
{
    Esp8266AnaState *s = ESP8266_ANA(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_ana_ops, s,
                          TYPE_ESP8266_ANA, sizeof(s->reg));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_esp8266_ana = {
    .name = TYPE_ESP8266_ANA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, Esp8266AnaState, ESP8266_ANA_BUSES),
        VMSTATE_UINT8_ARRAY(pll, Esp8266AnaState, ESP8266_ANA_PLL_REGS),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_ana_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_esp8266_ana;
    rc->phases.hold = esp8266_ana_reset_hold;
}

static const TypeInfo esp8266_ana_info = {
    .name = TYPE_ESP8266_ANA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266AnaState),
    .instance_init = esp8266_ana_init,
    .class_init = esp8266_ana_class_init,
};

static void esp8266_ana_register_types(void)
{
    type_register_static(&esp8266_ana_info);
}

type_init(esp8266_ana_register_types)

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
 * ESP8266 SPI0 flash controller (0x60000200) and the flash window at
 * 0x40200000, based on Max Filippov's ESP8266 model from OSLL/qemu-xtensa
 * (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "sysemu/block-backend.h"
#include "hw/ssi/esp8266_spi.h"

FIELD(SPI_CMD, USR, 18, 1)
FIELD(SPI_CMD, CE, 22, 1)
FIELD(SPI_CMD, BE, 23, 1)
FIELD(SPI_CMD, SE, 24, 1)
FIELD(SPI_CMD, PP, 25, 1)
FIELD(SPI_CMD, WRSR, 26, 1)
FIELD(SPI_CMD, RDSR, 27, 1)
FIELD(SPI_CMD, RDID, 28, 1)
FIELD(SPI_CMD, WRDI, 29, 1)
FIELD(SPI_CMD, WREN, 30, 1)
FIELD(SPI_CMD, READ, 31, 1)
FIELD(SPI_ADDR, OFFSET, 0, 24)
FIELD(SPI_ADDR, LENGTH, 24, 8)
FIELD(SPI_CTRL, ENABLE_AHB, 17, 1)
FIELD(SPI_STATUS, BUSY, 0, 1)
FIELD(SPI_STATUS, WRENABLE, 1, 1)
FIELD(SPI_CLOCK, CLKCNT_L, 0, 6)
FIELD(SPI_CLOCK, CLKCNT_H, 6, 6)
FIELD(SPI_CLOCK, CLKCNT_N, 12, 6)
FIELD(SPI_CLOCK, CLK_DIV_PRE, 18, 13)
FIELD(SPI_CLOCK, CLK_EQU_SYSCLK, 31, 1)
FIELD(SPI_USER, FLASH_MODE, 2, 1)
FIELD(SPI_USER2, COMMAND_VALUE, 0, 16)
FIELD(SPI_USER2, COMMAND_BITLEN, 28, 4)

static uint64_t esp8266_spi_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp8266SpiState *s = opaque;

    if (addr / 4 >= ESP8266_SPI_MAX || addr % 4 || size != 4) {
        return 0;
    }
    return s->reg[addr / 4];
}

/* JEDEC ID of a Winbond W25Qxx; the capacity byte is log2(size). */
#define ESP8266_FLASH_JEDEC_ID 0x0040ef

/* Keep the -drive image in step with the flash, so settings survive a run. */
static void esp8266_spi_flash_sync(Esp8266SpiState *s, uint32_t offset,
                                   uint32_t size)
{
    if (s->writable &&
        blk_pwrite(s->blk, offset, size,
                   (uint8_t *)s->flash_image + offset, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp8266: flash write-back failed at 0x%x\n", offset);
    }
}

static void esp8266_spi_flash_erase(Esp8266SpiState *s, uint32_t offset,
                                    uint32_t size)
{
    offset &= ~(size - 1);
    if (offset < s->flash_size) {
        size = MIN(size, s->flash_size - offset);
        memset((uint8_t *)s->flash_image + offset, 0xff, size);
        esp8266_spi_flash_sync(s, offset, size);
    }
}

static void esp8266_spi_cmd(Esp8266SpiState *s, hwaddr addr,
                            uint64_t val, unsigned size)
{
    uint32_t addr_reg = s->reg[ESP8266_SPI_FLASH_ADDR];
    uint32_t offset = FIELD_EX32(addr_reg, SPI_ADDR, OFFSET);
    uint32_t length = MIN(FIELD_EX32(addr_reg, SPI_ADDR, LENGTH), 64);
    bool writable = s->reg[ESP8266_SPI_FLASH_STATUS] &
                    R_SPI_STATUS_WRENABLE_MASK;

    if (val & R_SPI_CMD_READ_MASK) {
        if (offset + length <= s->flash_size) {
            memcpy(s->reg + ESP8266_SPI_FLASH_C0,
                   (uint8_t *)s->flash_image + offset, (length + 3) & ~3);
        }
    }
    if (val & R_SPI_CMD_RDID_MASK) {
        s->reg[ESP8266_SPI_FLASH_C0] = ESP8266_FLASH_JEDEC_ID |
                                       ctz32(s->flash_size) << 16;
    }
    if (val & R_SPI_CMD_RDSR_MASK) {
        /* Never busy; the write-enable latch is kept in STATUS. */
        s->reg[ESP8266_SPI_FLASH_STATUS] &= R_SPI_STATUS_WRENABLE_MASK;
    }
    if (writable && (val & R_SPI_CMD_PP_MASK) &&
        offset + length <= s->flash_size) {
        /* NOR program: bits only go from 1 to 0. */
        uint8_t *dst = (uint8_t *)s->flash_image + offset;
        const uint8_t *src = (const uint8_t *)(s->reg + ESP8266_SPI_FLASH_C0);

        for (uint32_t i = 0; i < length; i++) {
            dst[i] &= src[i];
        }
        esp8266_spi_flash_sync(s, offset, length);
    }
    if (writable && (val & R_SPI_CMD_SE_MASK)) {
        esp8266_spi_flash_erase(s, offset, 4 * KiB);
    }
    if (writable && (val & R_SPI_CMD_BE_MASK)) {
        esp8266_spi_flash_erase(s, offset, 64 * KiB);
    }
    if (writable && (val & R_SPI_CMD_CE_MASK)) {
        esp8266_spi_flash_erase(s, 0, s->flash_size);
    }
    if (val & (R_SPI_CMD_PP_MASK | R_SPI_CMD_SE_MASK |
               R_SPI_CMD_BE_MASK | R_SPI_CMD_CE_MASK |
               R_SPI_CMD_WRSR_MASK | R_SPI_CMD_WRDI_MASK)) {
        s->reg[ESP8266_SPI_FLASH_STATUS] &= ~R_SPI_STATUS_WRENABLE_MASK;
    }
    if (val & R_SPI_CMD_WREN_MASK) {
        s->reg[ESP8266_SPI_FLASH_STATUS] |= R_SPI_STATUS_WRENABLE_MASK;
    }
    if (val & R_SPI_CMD_USR_MASK) {
        uint32_t user2 = s->reg[ESP8266_SPI_FLASH_USER2];

        qemu_log_mask(LOG_UNIMP, "%s: user command %04x[%d bits]\n", __func__,
                      FIELD_EX32(user2, SPI_USER2, COMMAND_VALUE),
                      FIELD_EX32(user2, SPI_USER2, COMMAND_BITLEN));
    }
    /* Commands complete immediately: the CMD register reads back idle. */
    s->reg[ESP8266_SPI_FLASH_CMD] = 0;
}

static void esp8266_spi_write_ctrl(Esp8266SpiState *s, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    s->reg[ESP8266_SPI_FLASH_CTRL] = val;
    memory_region_set_enabled(&s->cache,
                              val & R_SPI_CTRL_ENABLE_AHB_MASK);
}

static void esp8266_spi_status(Esp8266SpiState *s, hwaddr addr,
                               uint64_t val, unsigned size)
{
}

static void esp8266_spi_clock(Esp8266SpiState *s, hwaddr addr,
                              uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: L: %d, H: %d, N: %d, PRE: %d, SYSCLK: %d\n",
              __func__,
              FIELD_EX32(val, SPI_CLOCK, CLKCNT_L),
              FIELD_EX32(val, SPI_CLOCK, CLKCNT_H),
              FIELD_EX32(val, SPI_CLOCK, CLKCNT_N),
              FIELD_EX32(val, SPI_CLOCK, CLK_DIV_PRE),
              FIELD_EX32(val, SPI_CLOCK, CLK_EQU_SYSCLK));
    s->reg[ESP8266_SPI_FLASH_CLOCK] = val;
}

static void esp8266_spi_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    Esp8266SpiState *s = opaque;
    static void (* const handler[])(Esp8266SpiState *s, hwaddr addr,
                                    uint64_t val, unsigned size) = {
        [ESP8266_SPI_FLASH_CMD] = esp8266_spi_cmd,
        [ESP8266_SPI_FLASH_CTRL] = esp8266_spi_write_ctrl,
        [ESP8266_SPI_FLASH_STATUS] = esp8266_spi_status,
        [ESP8266_SPI_FLASH_CLOCK] = esp8266_spi_clock,
    };

    if (addr / 4 >= ESP8266_SPI_MAX || addr % 4 || size != 4) {
        return;
    }
    if (addr / 4 < ARRAY_SIZE(handler) && handler[addr / 4]) {
        handler[addr / 4](s, addr, val, size);
    } else {
        s->reg[addr / 4] = val;
    }
}

static const MemoryRegionOps esp8266_spi_ops = {
    .read = esp8266_spi_read,
    .write = esp8266_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_spi_reset_hold(Object *obj, ResetType type)
{
    Esp8266SpiState *s = ESP8266_SPI(obj);

    memset(s->reg, 0, sizeof(s->reg));
    memory_region_set_enabled(&s->cache, false);
}

static void esp8266_spi_realize(DeviceState *dev, Error **errp)
{
    Esp8266SpiState *s = ESP8266_SPI(dev);
    int64_t size;

    if (!memory_region_init_rom(&s->cache, OBJECT(dev), "esp8266.flash",
                                ESP8266_SPI_FLASH_MAX, errp)) {
        return;
    }
    s->flash_image = memory_region_get_ram_ptr(&s->cache);
    memset(s->flash_image, 0xff, ESP8266_SPI_FLASH_MAX);
    memory_region_set_enabled(&s->cache, false);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->cache);

    s->flash_size = 1 * MiB;    /* ESP8285 embedded flash */
    if (!s->blk) {
        return;
    }
    size = blk_getlength(s->blk);
    if (size < 256 * KiB || size > ESP8266_SPI_FLASH_MAX ||
        !is_power_of_2(size) ||
        blk_pread(s->blk, 0, size, s->flash_image, 0) < 0) {
        error_setg(errp, "esp8266: the flash image must be 256 KiB to 16 MiB, "
                   "a power of two, and readable");
        return;
    }
    s->flash_size = size;
    if (blk_supports_write_perm(s->blk)) {
        if (blk_set_perm(s->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                         BLK_PERM_ALL, errp) < 0) {
            return;
        }
        s->writable = true;
    }
}

static void esp8266_spi_init(Object *obj)
{
    Esp8266SpiState *s = ESP8266_SPI(obj);

    memory_region_init_io(&s->iomem, obj, &esp8266_spi_ops, s,
                          TYPE_ESP8266_SPI, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_esp8266_spi = {
    .name = TYPE_ESP8266_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, Esp8266SpiState, ESP8266_SPI_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static Property esp8266_spi_properties[] = {
    DEFINE_PROP_DRIVE("drive", Esp8266SpiState, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp8266_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp8266_spi_realize;
    dc->vmsd = &vmstate_esp8266_spi;
    rc->phases.hold = esp8266_spi_reset_hold;
    device_class_set_props(dc, esp8266_spi_properties);
}

static const TypeInfo esp8266_spi_info = {
    .name = TYPE_ESP8266_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266SpiState),
    .instance_init = esp8266_spi_init,
    .class_init = esp8266_spi_class_init,
};

static void esp8266_spi_register_types(void)
{
    type_register_static(&esp8266_spi_info);
}

type_init(esp8266_spi_register_types)

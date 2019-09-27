/*
 * ESP32 SHA accelerator
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "crypto/hash.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/misc/esp32_sha.h"

#define ESP32_SHA_REGS_SIZE (A_SHA512_BUSY + 4)

static void esp32_sha_text_reg_byteswap_to(Esp32ShaState* s, uint32_t* dst, size_t len_words)
{
    for (int i = 0; i < len_words; ++i) {
        dst[i] = __builtin_bswap32(s->text[i]);
    }
}

static uint64_t esp32_sha_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32ShaState *s = ESP32_SHA(opaque);
    uint64_t r = 0;
    switch (addr) {
    case 0 ... (ESP32_SHA_TEXT_REG_CNT - 1) * sizeof(uint32_t):
        r = s->text[addr / sizeof(uint32_t)];
        break;
    }
    return r;
}

static void esp32_sha_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32ShaState *s = ESP32_SHA(opaque);
    switch (addr) {
    case 0 ... (ESP32_SHA_TEXT_REG_CNT - 1) * sizeof(uint32_t):
        s->text[addr / sizeof(uint32_t)] = value;
        break;
    case A_SHA1_START:
        sha1_init(&s->sha1);
        esp32_sha_text_reg_byteswap_to(s, (uint32_t *) s->text, 16);
        sha1_compress((uint32_t *)&s->sha1.state, (unsigned char *)s->text);
        break;
    case A_SHA256_START:
        sha256_init(&s->sha256);
        esp32_sha_text_reg_byteswap_to(s, (uint32_t *) s->text, 16);
        sha256_compress(&s->sha256, (unsigned char *)s->text);
        break;
    case A_SHA384_START:
        sha384_init(&s->sha512);
        esp32_sha_text_reg_byteswap_to(s, (uint32_t *) s->text, 32);
        sha512_compress(&s->sha512, (unsigned char *)s->text);
        break;
    case A_SHA512_START:
        sha512_init(&s->sha512);
        esp32_sha_text_reg_byteswap_to(s, (uint32_t *) s->text, 32);
        sha512_compress(&s->sha512, (unsigned char *)s->text);
        break;
    case A_SHA1_CONTINUE:
        esp32_sha_text_reg_byteswap_to(s, (uint32_t *) s->text, 16);
        sha1_compress((uint32_t *)&s->sha1.state, (unsigned char *)s->text);
        break;
    case A_SHA256_CONTINUE:
        esp32_sha_text_reg_byteswap_to(s, (uint32_t *) s->text, 16);
        sha256_compress(&s->sha256, (unsigned char *)s->text);
        break;
    case A_SHA384_CONTINUE:
    case A_SHA512_CONTINUE:
        esp32_sha_text_reg_byteswap_to(s, (uint32_t *) s->text, 32);
        sha512_compress(&s->sha512, (unsigned char *)s->text);
        break;
    case A_SHA1_LOAD:
        for (int i = 0; i < 5; i++) {
            s->text[i] = s->sha1.state[i];
        }
        break;
    case A_SHA256_LOAD:
        for (int i = 0; i < 8; i++) {
            s->text[i] = s->sha256.state[i];
        }
        break;
    case A_SHA384_LOAD:
    case A_SHA512_LOAD:
        for (int i = 0; i < 8; i++) {
            s->text[i * 2] = (uint32_t)(s->sha512.state[i] >> 32);
            s->text[1 + (i * 2)] = (uint32_t)(s->sha512.state[i] & 0xffffffff);
        }
        break;
    }
}

static const MemoryRegionOps esp32_sha_ops = {
    .read =  esp32_sha_read,
    .write = esp32_sha_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_sha_init(Object *obj)
{
    Esp32ShaState *s = ESP32_SHA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_sha_ops, s,
                          TYPE_ESP32_SHA, ESP32_SHA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const TypeInfo esp32_sha_info = {
    .name = TYPE_ESP32_SHA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32ShaState),
    .instance_init = esp32_sha_init,
};

static void esp32_sha_register_types(void)
{
    type_register_static(&esp32_sha_info);
}

type_init(esp32_sha_register_types)

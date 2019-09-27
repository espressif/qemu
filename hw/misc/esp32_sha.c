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

/* QEMU hash API includes only the "qcrypto_hash_bytes" function which takes
 * bytes as input, and calculates the digest. It doesn't allow "updating"
 * the state multiple times with blocks of input. Therefore we collect all
 * the input in an array (s->full_text) and when SHA_X_LOAD_REG is set,
 * we call "qcrypto_hash_bytes" to get the digest.
 */

static void esp32_sha_text_reg_byteswap_to(Esp32ShaState* s, uint32_t* dst, size_t len_words)
{
    for (int i = 0; i < len_words; ++i) {
        dst[i] = __builtin_bswap32(s->text[i]);
    }
}


static inline QCryptoHashAlgorithm algorithm_for_addr(hwaddr reg_addr)
{
    const QCryptoHashAlgorithm hash_alg_for_reg[] = {
        QCRYPTO_HASH_ALG_SHA1,
        QCRYPTO_HASH_ALG_SHA256,
        QCRYPTO_HASH_ALG_SHA384,
        QCRYPTO_HASH_ALG_SHA512
    };
    size_t index = (reg_addr - A_SHA1_START) / 0x10;
    return hash_alg_for_reg[index];
}

static uint32_t hash_block_bytes[] = {
    [QCRYPTO_HASH_ALG_SHA1] = 64,
    [QCRYPTO_HASH_ALG_SHA256] = 64,
    [QCRYPTO_HASH_ALG_SHA384] = 128,
    [QCRYPTO_HASH_ALG_SHA512] = 128,
};

static void esp32_sha_update_text(Esp32ShaState* s, QCryptoHashAlgorithm hash_alg)
{
    uint32_t block_len_bytes = hash_block_bytes[hash_alg];
    if (s->full_text_len + block_len_bytes > s->full_text_reserved) {
        uint32_t full_text_reserved = MAX(s->full_text_reserved * 2, s->full_text_reserved + block_len_bytes);
        uint8_t *new_full_text = g_realloc(s->full_text, full_text_reserved);
        s->full_text_reserved = full_text_reserved;
        s->full_text = new_full_text;
    }
    esp32_sha_text_reg_byteswap_to(s, (uint32_t*) (s->full_text + s->full_text_len), block_len_bytes/4);
    s->full_text_len += block_len_bytes;
}

static void esp32_sha_finish(Esp32ShaState *s, QCryptoHashAlgorithm hash_alg)
{
    /* ESP32 SHA accelerator accepts padded blocks (doesn't do any extra padding), but
     * qcrypto_hash_bytes adds padding after the last block. Remove the padding by checking
     * the bit count in the last block. This may give results different from what the hardware
     * would give if the guest application pads the block incorrectly.
     */
    uint8_t *result = (uint8_t*) s->text;
    size_t result_len = qcrypto_hash_digest_len(hash_alg);
    assert(result_len <= sizeof(s->text));
    if (s->full_text_len % hash_block_bytes[hash_alg] != 0) {
        error_report("esp32_sha_finish: invalid text length %" PRIx32 "\n", s->full_text_len);
    } else {
        uint32_t byte_count;
        memcpy(&byte_count, s->full_text + s->full_text_len - sizeof(byte_count), sizeof(byte_count));
        byte_count = __builtin_bswap32(byte_count) / 8;
        if (byte_count > s->full_text_len) {
            error_report("esp32_sha_finish: invalid byte count %" PRIx32 "\n", byte_count);
        } else {
            qcrypto_hash_bytes(hash_alg, (const char*) s->full_text, byte_count, &result, &result_len, &error_abort);
            esp32_sha_text_reg_byteswap_to(s, (uint32_t*) s->text, result_len/4);
        }
    }
    g_free(s->full_text);
    s->full_text = NULL;
    s->full_text_len = 0;
    s->full_text_reserved = 0;
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
    case A_SHA1_CONTINUE:
    case A_SHA256_START:
    case A_SHA256_CONTINUE:
    case A_SHA384_START:
    case A_SHA384_CONTINUE:
    case A_SHA512_START:
    case A_SHA512_CONTINUE:
        esp32_sha_update_text(s, algorithm_for_addr(addr));
        break;
    case A_SHA1_LOAD:
    case A_SHA256_LOAD:
    case A_SHA384_LOAD:
    case A_SHA512_LOAD:
        esp32_sha_finish(s, algorithm_for_addr(addr));
        break;
    }
}

static const MemoryRegionOps esp32_sha_ops = {
    .read =  esp32_sha_read,
    .write = esp32_sha_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_sha_reset(DeviceState *dev)
{
    Esp32ShaState *s = ESP32_SHA(dev);
    g_free(s->full_text);
    s->full_text = NULL;
    s->full_text_len = 0;
    s->full_text_reserved = 0;
}

static void esp32_sha_init(Object *obj)
{
    Esp32ShaState *s = ESP32_SHA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_sha_ops, s,
                          TYPE_ESP32_SHA, ESP32_SHA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32_sha_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_sha_reset;
}

static const TypeInfo esp32_sha_info = {
    .name = TYPE_ESP32_SHA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32ShaState),
    .instance_init = esp32_sha_init,
    .class_init = esp32_sha_class_init
};

static void esp32_sha_register_types(void)
{
    type_register_static(&esp32_sha_info);
}

type_init(esp32_sha_register_types)

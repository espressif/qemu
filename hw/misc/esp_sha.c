/*
 * ESP SHA accelerator
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/dma/esp_gdma.h"
#include "hw/misc/esp_sha.h"
#include "hw/irq.h"

#define SHA_WARNING 0
#define SHA_DEBUG 0

static ESPHashAlg esp_sha_algs[] = {
    [ESP_SHA_1_MODE]    = {
        .init     = (hash_init) sha1_init,
        .compress = (hash_compress) sha1_compress,
        .len      = sizeof(struct sha1_state)
    },
    [ESP_SHA_224_MODE]  = {
        .init     = (hash_init) sha224_init,
        .compress = (hash_compress) sha224_compress,
        .len      = sizeof(struct sha256_state)
    },
    [ESP_SHA_256_MODE]  = {
        .init     = (hash_init) sha256_init,
        .compress = (hash_compress) sha256_compress,
        .len      = sizeof(struct sha256_state)
    },
    [ESP_SHA_384_MODE]  = {
        .init     = (hash_init) sha384_init,
        .compress = (hash_compress) sha512_compress,
        .len      = SHA384_HASH_SIZE
    },
    [ESP_SHA_512_MODE]  = {
        .init     = (hash_init) sha512_init,
        .compress = (hash_compress) sha512_compress,
        .len      = sizeof(struct sha512_state)
    },
    [ESP_SHA_512_224_MODE]  = {
        .init     = (hash_init) sha512_224_init,
        .compress = (hash_compress) sha512_compress,
        .len      = sizeof(struct sha512_state)
    },
    [ESP_SHA_512_256_MODE]  = {
        .init     = (hash_init) sha512_256_init,
        .compress = (hash_compress) sha512_compress,
        .len      = sizeof(struct sha512_state)
    },
    [ESP_SHA_512_t_MODE]  = {
        .init         = (hash_init) sha512_t_init,
        .init_message = (hash_init_message) sha512_t_init_message,
        .compress     = (hash_compress) sha512_compress,
        .len          = sizeof(struct sha512_state)
    },
};


static void esp_sha_write_digest(ESPShaMode mode, uint32_t* hash, ESPHashContext* context, size_t len)
{
    if (mode < ESP_SHA_384_MODE) {
        memcpy(context, hash, len);
    } else {
        for (int i = 0; i < 8; i++) {
            context->sha512.state[i] = ((uint64_t)hash[i * 2] << 32) | hash[1 + (i * 2)];
        }
    }
}


static void esp_sha_read_digest(ESPShaMode mode, uint32_t* hash, ESPHashContext* context, size_t len)
{
    if (mode < ESP_SHA_384_MODE) {
        memcpy(hash, context, len);
    } else {
        for (int i = 0; i < 8; i++) {
            hash[i * 2] = (uint32_t)(context->sha512.state[i] >> 32);
            hash[1 + (i * 2)] = (uint32_t)(context->sha512.state[i] & 0xffffffff);
        }
    }
}


static void esp_sha_continue_hash(ESPShaState *s, uint32_t mode, uint32_t *message, uint32_t *hash)
{
    assert(mode <= ESP_SHA_512_t_MODE);
    ESPHashAlg alg = esp_sha_algs[mode];

    alg.compress(&s->context, (uint8_t*) message);

    esp_sha_read_digest(mode, hash, &s->context, alg.len);
}


static void esp_sha_continue_dma(ESPShaState *s)
{
    assert(s->mode <= ESP_SHA_512_t_MODE);
    ESPHashAlg alg = esp_sha_algs[s->mode];
    uint32_t gdma_out_idx = 0;

    assert(alg.compress);

    size_t blk_len = (s->mode < ESP_SHA_384_MODE) ? 64 : 128;

    /* Number of blocks to process, each block is blk_len bytes big */
    const uint32_t blocks = s->block;
    const uint32_t buf_size = blocks * blk_len;

    /* Get the GDMA channel connected to SHA module.
     * Specify ESP_GDMA_OUT_IDX since the data are going OUT of GDMA but IN our current component. */
    if ( !esp_gdma_get_channel_periph(s->gdma, GDMA_SHA, ESP_GDMA_OUT_IDX, &gdma_out_idx) )
    {
        warn_report("[SHA] GDMA requested but no properly configured channel found");
        return;
    }

    /* Allocate the buffer that will contain the data and get teh actual data */
    uint8_t *buffer = g_malloc(blocks * blk_len);
    if (buffer == NULL)
    {
        error_report("[SHA] No more memory in host!");
        return;
    }
    if ( !esp_gdma_read_channel(s->gdma, gdma_out_idx, buffer, buf_size) ) {
        warn_report("[SHA] Error reading from GDMA buffer");
        g_free(buffer);
        return;
    }

    /* Perform the actual SHA operation on the whole buffer */
    for (uint32_t i = 0; i < blocks; i++)
    {
        alg.compress(&s->context, buffer + i * blk_len);
    }

    esp_sha_read_digest(s->mode, s->hash, &s->context, alg.len);

    g_free(buffer);

    /* Trigger an interrupt if enabled! */
    if (s->int_ena) {
        qemu_irq_raise(s->irq);
    }
}


static void esp_sha_start(ESPShaState *s, ESPShaOperation op, uint32_t mode, uint32_t *message, uint32_t *hash)
{
    ESPHashAlg alg = esp_sha_algs[mode];
    assert(alg.init && alg.compress);

    if ((op & SHA_OP_TYPE_MASK) == OP_START) {
        alg.init(&s->context);
        if (s->mode == ESP_SHA_512_t_MODE) {
            alg.init_message(message, ESP_SHA_MAX_MESSAGE_WORDS, s->t, s->t_len);
        }
    } else {
        /* Continue operation: initialize the context from the current hash.
         * We don't have any accessor to do it so ... do it the "dirty" way */
        esp_sha_write_digest(mode, hash, &s->context, alg.len);
    }

    if ((op & SHA_OP_DMA_MASK) == SHA_OP_DMA_MASK) {
        esp_sha_continue_dma(s);
    } else {
        esp_sha_continue_hash(s, mode, message, hash);
    }
}


static uint64_t esp_sha_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPShaClass *class = ESP_SHA_GET_CLASS(opaque);
    ESPShaState *s = ESP_SHA(opaque);
    hwaddr index = 0;

    uint64_t r = 0;
    switch (addr) {
    case A_SHA_MODE:
        r = s->mode;
        break;
    case A_SHA_BUSY:
        /* SHA driver is never busy as calculation happens synchronously */
        r = 0;
        break;
    case A_SHA_DATE:
        /* Hardcode the version control register for now */
        r = class->date;
        break;
    case A_SHA_H_MEM ... A_SHA_M_MEM - 1:
        index = (addr - A_SHA_H_MEM) / sizeof(uint32_t);
        r = bswap32(s->hash[index]);
        break;
    case A_SHA_M_MEM ... ESP_SHA_REGS_SIZE - 1:
        index = (addr - A_SHA_M_MEM) / sizeof(uint32_t);
        r = s->message[index];
        break;
    case A_SHA_DMA_BLOCK_NUM:
        r = s->block;
        break;
    case A_SHA_IRQ_ENA:
        r = s->int_ena ? 1 : 0;
        break;
    default:
#if SHA_WARNING
        warn_report("[ESP] SHA DMA and IRQ unsupported for now, ignoring...\n");
#endif
        break;
    }

#if SHA_DEBUG
    info_report("[ESP] SHA reading %08lx (%08lx)", addr, r);
#endif

    return r;
}


static void esp_sha_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESPShaClass *class = ESP_SHA_GET_CLASS(opaque);
    ESPShaState *s = ESP_SHA(opaque);
    hwaddr index = 0;

#if SHA_DEBUG
    info_report("[ESP] SHA writing %08lx (%08lx)", addr, value);
#endif

    switch (addr) {
    case A_SHA_MODE:
        /* Make sure the value is always between 0 and 7 as the real hardware doesn't
         * accept a value of 8. Choose SHA-1 by default in that case. */
        s->mode = (value & 0b111) % 8;
        break;

    case A_SHA_T_STRING:
        s->t = bswap32((uint32_t) value);
        break;

    case A_SHA_T_LENGTH:
        s->t_len = bswap32(FIELD_EX32(value, SHA_T_LENGTH, T_LENGTH));
        break;

    case A_SHA_START:
        if (FIELD_EX32(value, SHA_START, START)) {
            class->sha_start(s, OP_START, s->mode, s->message, s->hash);
        }
        break;

    case A_SHA_CONTINUE:
        if (FIELD_EX32(value, SHA_CONTINUE, CONTINUE)) {
            class->sha_start(s, OP_CONTINUE, s->mode, s->message, s->hash);
        }
        break;

    case A_SHA_H_MEM ... A_SHA_M_MEM - 1:
        /* Only support word aligned access for the moment */
        if (size != sizeof(uint32_t)) {
            error_report("[SHA] Only 32-bit word access supported at the moment");
        }
        index = (addr - A_SHA_H_MEM) / sizeof(uint32_t);
        s->hash[index] = bswap32((uint32_t) value);
        break;

    case A_SHA_M_MEM ... ESP_SHA_REGS_SIZE - 1:
        index = (addr - A_SHA_M_MEM) / sizeof(uint32_t);
        s->message[index] = (uint32_t) value;
        break;

    case A_SHA_DMA_BLOCK_NUM:
        s->block = FIELD_EX32(value, SHA_DMA_BLOCK_NUM, DMA_BLOCK_NUM);
        break;

    case A_SHA_DMA_START:
        if (FIELD_EX32(value, SHA_DMA_START, DMA_START)) {
            class->sha_start(s, OP_DMA_START, s->mode, s->message, s->hash);
        }
        break;

    case A_SHA_DMA_CONTINUE:
        if (FIELD_EX32(value, SHA_DMA_CONTINUE, DMA_CONTINUE)) {
            class->sha_start(s, OP_DMA_CONTINUE, s->mode, s->message, s->hash);
        }
        break;

    case A_SHA_CLEAR_IRQ:
        qemu_irq_lower(s->irq);
        break;

    case A_SHA_IRQ_ENA:
        s->int_ena = FIELD_EX32(value, SHA_IRQ_ENA, INTERRUPT_ENA) != 0;
        break;

    default:
#if SHA_WARNING
        /* Unsupported for now, do nothing */
        warn_report("[SHA] Unsupported write to %08lx\n", addr);
#endif
        break;
    }
}

static const MemoryRegionOps esp_sha_ops = {
    .read =  esp_sha_read,
    .write = esp_sha_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp_sha_reset_hold(Object *obj, ResetType type)
{
    ESPShaState *s = ESP_SHA(obj);
    memset(s->hash, 0, 8 * sizeof(uint32_t));
    memset(s->message, 0, ESP_SHA_MAX_MESSAGE_WORDS * sizeof(uint32_t));

    s->block = 0;
    s->int_ena = 0;
    qemu_irq_lower(s->irq);
}


static void esp_sha_realize(DeviceState *dev, Error **errp)
{
    ESPShaState *s = ESP_SHA(dev);

    /* Make sure GDMA was set of issue an error */
    if (s->gdma == NULL) {
        error_report("[SHA] GDMA controller must be set!");
    }
}


static void esp_sha_init(Object *obj)
{
    ESPShaState *s = ESP_SHA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_sha_ops, s,
                          TYPE_ESP_SHA, ESP_SHA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);
}

static void esp_sha_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESPShaClass* esp_sha = ESP_SHA_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp_sha_realize;
    rc->phases.hold = esp_sha_reset_hold;

    esp_sha->sha_start = esp_sha_start;
}

static const TypeInfo esp_sha_info = {
    .name = TYPE_ESP_SHA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPShaState),
    .instance_init = esp_sha_init,
    .class_init = esp_sha_class_init,
    .class_size = sizeof(ESPShaClass),
    .abstract = true
};

static void esp_sha_register_types(void)
{
    type_register_static(&esp_sha_info);
}

type_init(esp_sha_register_types)

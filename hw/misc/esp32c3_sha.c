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
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32c3_sha.h"
#include "hw/irq.h"

#define SHA_WARNING 0
#define SHA_DEBUG 0

static ESP32C3HashAlg esp32c3_algs[] = {
    [ESP32C3_SHA_1_MODE]    = {
        .init     = (hash_init) sha1_init,
        .compress = (hash_compress) sha1_compress,
        .len      = sizeof(struct sha1_state)
    },
    [ESP32C3_SHA_224_MODE]  = {
        .init     = (hash_init) sha224_init,
        .compress = (hash_compress) sha224_compress,
        .len      = SHA224_HASH_SIZE
    },
    [ESP32C3_SHA_256_MODE]  = {
        .init     = (hash_init) sha256_init,
        .compress = (hash_compress) sha256_compress,
        .len      = sizeof(struct sha256_state)
    },
};


static void esp32c3_sha_continue_hash(ESP32C3ShaState *s, uint32_t mode, uint32_t *message, uint32_t *hash)
{
    assert(mode <= ESP32C3_SHA_256_MODE);
    ESP32C3HashAlg alg = esp32c3_algs[mode];

    alg.compress(&s->context, (uint8_t*) message);
    memcpy(hash, &s->context, alg.len);
}


static void esp32c3_sha_continue_dma(ESP32C3ShaState *s)
{
    assert(s->mode <= ESP32C3_SHA_256_MODE);
    ESP32C3HashAlg alg = esp32c3_algs[s->mode];
    uint32_t gdma_out_idx = 0;

    assert(alg.compress);

    /* Number of blocks to process, each block is ESP32C3_MESSAGE_SIZE bytes big */
    const uint32_t blocks = s->block;
    const uint32_t buf_size = blocks * ESP32C3_MESSAGE_SIZE;

    /* Get the GDMA channel connected to SHA module.
     * Specify ESP32C3_GDMA_OUT_IDX since the data are going OUT of GDMA but IN our current component. */
    if ( !esp32c3_gdma_get_channel_periph(s->gdma, GDMA_SHA, ESP32C3_GDMA_OUT_IDX, &gdma_out_idx) )
    {
        warn_report("[SHA] GDMA requested but no properly configured channel found");
        return;
    }

    /* Allocate the buffer that will contain the data and get teh actual data */
    uint8_t *buffer = g_malloc(blocks * ESP32C3_MESSAGE_SIZE);
    if (buffer == NULL)
    {
        error_report("[SHA] No more memory in host!");
        return;
    }

    if ( !esp32c3_gdma_read_channel(s->gdma, gdma_out_idx, buffer, buf_size) ) {
        warn_report("[SHA] Error reading from GDMA buffer");
        g_free(buffer);
    }

    /* Perform the actual SHA operation on the whole buffer */
    for (uint32_t i = 0; i < blocks; i++)
    {
        alg.compress(&s->context, buffer + i * ESP32C3_MESSAGE_SIZE);
    }

    memcpy(s->hash, &s->context, alg.len);

    g_free(buffer);

    /* Trigger an interrupt if enabled! */
    if (s->int_ena) {
        qemu_irq_raise(s->irq);
    }
}


static void esp32c3_sha_start(ESP32C3ShaState *s, ESP32C3ShaOperation op, uint32_t mode, uint32_t *message, uint32_t *hash)
{
    ESP32C3HashAlg alg = esp32c3_algs[mode];
    assert(alg.init && alg.compress);

    if ((op & SHA_OP_TYPE_MASK) == OP_START) {
        alg.init(&s->context);
    } else {
        /* Continue operation: initialize the context from the current hash.
         * We don't have any accessor to do it so ... do it the "dirty" way */
        memcpy(&s->context, hash, alg.len);
    }

    if ((op & SHA_OP_DMA_MASK) == SHA_OP_DMA_MASK) {
        esp32c3_sha_continue_dma(s);
    } else {
        esp32c3_sha_continue_hash(s, mode, message, hash);
    }
}


static uint64_t esp32c3_sha_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3ShaState *s = ESP32C3_SHA(opaque);
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
        r = 0x20190402;
        break;
    case A_SHA_H_MEM ... A_SHA_M_MEM - 1:
        index = (addr - A_SHA_H_MEM) / sizeof(uint32_t);
        r = bswap32(s->hash[index]);
        break;
    case A_SHA_M_MEM ... ESP32C3_SHA_REGS_SIZE - 1:
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
        warn_report("[ESP32-C3] SHA DMA and IRQ unsupported for now, ignoring...\n");
#endif
        break;
    }

#if SHA_DEBUG
    info_report("[ESP32-C3] SHA reading %08lx (%08lx)", addr, r);
#endif

    return r;
}


static void esp32c3_sha_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32C3ShaClass *class = ESP32C3_SHA_GET_CLASS(opaque);
    ESP32C3ShaState *s = ESP32C3_SHA(opaque);
    hwaddr index = 0;

#if SHA_DEBUG
    info_report("[ESP32-C3] SHA writing %08lx (%08lx)", addr, value);
#endif

    switch (addr) {
    case A_SHA_MODE:
        /* Make sure the value is always between 0 and 2 as the real hardware doesn't
         * accept a value of 3. Choose SHA-1 by default in that case. */
        s->mode = (value & 0b11) % 3;
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

    case A_SHA_M_MEM ... ESP32C3_SHA_REGS_SIZE - 1:
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

static const MemoryRegionOps esp32c3_sha_ops = {
    .read =  esp32c3_sha_read,
    .write = esp32c3_sha_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp32c3_sha_reset(DeviceState *dev)
{
    ESP32C3ShaState *s = ESP32C3_SHA(dev);
    memset(s->hash, 0, 8 * sizeof(uint32_t));
    memset(s->message, 0, ESP32C3_MESSAGE_WORDS * sizeof(uint32_t));

    s->block = 0;
    s->int_ena = 0;
    qemu_irq_lower(s->irq);
}


static void esp32c3_sha_realize(DeviceState *dev, Error **errp)
{
    ESP32C3ShaState *s = ESP32C3_SHA(dev);

    /* Make sure GDMA was set of issue an error */
    if (s->gdma == NULL) {
        error_report("[SHA] GDMA controller must be set!");
    }
}


static void esp32c3_sha_init(Object *obj)
{
    ESP32C3ShaState *s = ESP32C3_SHA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_sha_ops, s,
                          TYPE_ESP32C3_SHA, ESP32C3_SHA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);
}

static void esp32c3_sha_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32C3ShaClass* esp32c3_sha = ESP32C3_SHA_CLASS(klass);

    dc->realize = esp32c3_sha_realize;
    dc->reset = esp32c3_sha_reset;

    esp32c3_sha->sha_start = esp32c3_sha_start;
}

static const TypeInfo esp32c3_sha_info = {
    .name = TYPE_ESP32C3_SHA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3ShaState),
    .instance_init = esp32c3_sha_init,
    .class_init = esp32c3_sha_class_init,
    .class_size = sizeof(ESP32C3ShaClass)
};

static void esp32c3_sha_register_types(void)
{
    type_register_static(&esp32c3_sha_info);
}

type_init(esp32c3_sha_register_types)

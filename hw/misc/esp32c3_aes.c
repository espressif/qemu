/*
 * ESP32-C3 AES emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c3_aes.h"
#include "qemu/error-report.h"
#include <gcrypt.h>
#include "qemu/bswap.h"
#include "hw/irq.h"
#include "crypto/aes.h"

#define AES_WARNING 0
#define AES_DEBUG   0


static void esp32c3_aes_dma_exit(ESP32C3AesState *s)
{
    s->state_reg = ESP32C3_AES_IDLE;
}

static void* esp32c3_aes_get_buffer(uint32_t size)
{
    /* Instead of reallocating a buffer every time, keep a watermark and a single buffer */
    static void* buffer = NULL;
    static uint32_t buf_size = 0;

    if (buf_size < size) {
        buffer = g_realloc(buffer, size);
        buf_size = size;
    }

    return buffer;
}


/**
 * @brief Interpret data in IV memory as a counter and add a block count to its value.
 *        Used in CTR block mode.
 */
static void esp32c3_aes_ctr_add_counter(ESP32C3AesState *s, uint32_t blocks)
{
    /* Check the length of this counter in bits. In both cases, it is stored in BIG-ENDIAN */
    if (FIELD_EX32(s->inc_sel_reg, AES_INC_SEL_REG, AES_INC_SEL) == 1) {
        /* 128-bit mode, no native 128 integer type, use two 64-bit types */
        uint64_t *low_ptr = (uint64_t*) (s->iv_mem + sizeof(uint64_t));
        uint64_t *high_ptr = (uint64_t*) s->iv_mem;
        const uint64_t original = be64_to_cpu(*low_ptr);
        uint64_t value = original + blocks;
        *low_ptr = cpu_to_be64(value);
        /* If the value overflowed, we have to update the upper part too */
        if (original > value) {
            value = be64_to_cpu(*high_ptr) + 1;
            *high_ptr = cpu_to_be64(value);
        }
    } else {
        /* 32-bit mode */
        uint32_t *counter_ptr = (uint32_t*) &s->iv_mem[ESP32C3_AES_IV_REG_CNT - sizeof(uint32_t)];
        const uint32_t value = be32_to_cpu(*counter_ptr) + blocks;
        *counter_ptr = cpu_to_be32(value);
    }
}


static void esp32c3_aes_dma_start(ESP32C3AesState *s)
{
    gcry_cipher_hd_t ghandle;
    uint32_t gdma_out_idx;
    uint32_t gdma_in_idx;

    const enum gcry_cipher_modes cipher_map[ESP32C3_AES_CIPHER_COUNT] = {
        [ESP32C3_AES_ECB_CIPHER]    = GCRY_CIPHER_MODE_ECB,
        [ESP32C3_AES_CBC_CIPHER]    = GCRY_CIPHER_MODE_CBC,
        [ESP32C3_AES_OFB_CIPHER]    = GCRY_CIPHER_MODE_OFB,
        [ESP32C3_AES_CTR_CIPHER]    = GCRY_CIPHER_MODE_CTR,
        [ESP32C3_AES_CFB8_CIPHER]   = GCRY_CIPHER_MODE_CFB8,
        [ESP32C3_AES_CFB128_CIPHER] = GCRY_CIPHER_MODE_CFB,
    };

    /* Get the block Cipher mode */
    const uint32_t cipher_mode = FIELD_EX32(s->block_mode_reg , AES_BLK_MODE_REG, AES_BLOCK_MODE);

    /* Check whether we have to encrypt or decrypt */
    const uint32_t mode = FIELD_EX32(s->mode_reg , AES_MODE_REG, AES_MODE);
    const bool encrypt = (mode == ESP32C3_AES_MODE_128_ENC) || (mode == ESP32C3_AES_MODE_256_ENC);
    const bool decrypt = (mode == ESP32C3_AES_MODE_128_DEC) || (mode == ESP32C3_AES_MODE_256_DEC);

    /* Get the length, in bits of the key */
    const int length = (mode == ESP32C3_AES_MODE_128_ENC || mode == ESP32C3_AES_MODE_128_DEC) ? 128 : 256;
    const int algo = length == 128 ? GCRY_CIPHER_AES128 : GCRY_CIPHER_AES256;

    if (cipher_mode >= ESP32C3_AES_CIPHER_COUNT) {
        error_report("[AES] Invalid or unsupported Cipher block mode!");
        return;
    } else if (!decrypt && !encrypt) {
        error_report("[AES] Invalid mode!");
        return;
    }

    gcry_error_t err = gcry_cipher_open(&ghandle, algo, cipher_map[cipher_mode], 0);
    if (err) {
        error_report("[AES] error 0x%x when opening cipher", err);
        return;
    }

    /* Cast the keys and data to byte array.
     * This can only work as-is if the host computer is has a little-endian CPU.  */
    const uint8_t *key = (uint8_t*) &s->key;
    uint8_t *iv_mem = (uint8_t*) &s->iv_mem;

    /* Set the algorithm key */
    err = gcry_cipher_setkey(ghandle, key, length / 8);
    if (err) {
        error_report("[AES] error 0x%x setting key", err);
        goto close_exit;
    }

    /* `iv_mem` field represents the Initialization Vector for CBC/OFB/CFB operations
     * But it represents the Initial Counter Block for CTR operation.
     * It shall be ignored for ECB block operation. */
    if (cipher_mode == ESP32C3_AES_CTR_CIPHER) {
        err = gcry_cipher_setctr(ghandle, iv_mem, ESP32C3_AES_IV_REG_CNT);
    } else if (cipher_mode != ESP32C3_AES_ECB_CIPHER) {
        err = gcry_cipher_setiv(ghandle, iv_mem, ESP32C3_AES_IV_REG_CNT);
    }

    if (err) {
        error_report("[AES] error 0x%x setting IV memory", err);
        goto close_exit;
    }

    /* Get the GDMA input channel index for AES peripheral */
    assert(s->gdma != NULL);

    if ( !esp32c3_gdma_get_channel_periph(s->gdma, GDMA_AES, ESP32C3_GDMA_OUT_IDX, &gdma_out_idx) ||
         !esp32c3_gdma_get_channel_periph(s->gdma, GDMA_AES, ESP32C3_GDMA_IN_IDX, &gdma_in_idx) ) {
        warn_report("[AES] GDMA requested but no properly configured channel found");
        goto close_exit;
    }

    /* Block number represents the number of 128-bit (16-byte) blocks to encrypt.
     * If block_num_reg is 100, we have to encrypt 100*128/8 = 1600 bytes */
    uint32_t buf_size = s->block_num_reg * 16;
    uint8_t *buffer = esp32c3_aes_get_buffer(buf_size);

    if ( !esp32c3_gdma_read_channel(s->gdma, gdma_out_idx, buffer, buf_size) ) {
        warn_report("[AES] Error reading from GDMA buffer");
        goto close_exit;
    }

    /* Reading was successful, process the buffer (encrypt/decrypt) and write back to the GDMA OUT buffer */
    if (encrypt) {
        err = gcry_cipher_encrypt(ghandle, buffer, buf_size, NULL, 0);

        if (cipher_mode != ESP32C3_AES_CTR_CIPHER) {
            /* On the real hardware, IV memory is used in-place for encrypting data, so copy the last encrypted block to IV memory */
            memcpy(iv_mem, buffer + buf_size - 16, ESP32C3_AES_IV_REG_CNT);
        }
    } else {
        /* Store the last block of plaintext, needed for OFB */
        const uint8_t *buffer_last_block = buffer + buf_size - ESP32C3_AES_IV_REG_CNT;
        uint8_t plaintext[ESP32C3_AES_IV_REG_CNT];
        memcpy(plaintext, buffer_last_block, ESP32C3_AES_IV_REG_CNT);

        /* The IV memory is initalized with the encrypted data, so do the copy now */
        if (cipher_mode != ESP32C3_AES_OFB_CIPHER && cipher_mode != ESP32C3_AES_CTR_CIPHER) {
            memcpy(iv_mem, buffer + buf_size - 16, ESP32C3_AES_IV_REG_CNT);
        }

        err = gcry_cipher_decrypt(ghandle, buffer, buf_size, NULL, 0);

        /* For OFB, it is done after the decryption. Moreover, the hardware XOR the original plaintext with the output
         * and stores the result in IV memory. */
        if (cipher_mode == ESP32C3_AES_OFB_CIPHER) {
            for (int i = 0; i < ESP32C3_AES_IV_REG_CNT; i++) {
                iv_mem[i] = buffer_last_block[i] ^ plaintext[i];
            }
        }
    }

    if (cipher_mode == ESP32C3_AES_CTR_CIPHER) {
        esp32c3_aes_ctr_add_counter(s, s->block_num_reg);
    }

    if (err) {
        error_report("[AES] error processing memory");
        goto close_exit;
    }

    if ( !esp32c3_gdma_write_channel(s->gdma, gdma_in_idx, buffer, buf_size) ) {
        warn_report("[AES] Error writing to GDMA buffer");
        goto close_exit;
    }

    s->state_reg = ESP32C3_AES_DONE;

    if (s->int_ena_reg) {
        qemu_irq_raise(s->irq);
    }

close_exit:
    gcry_cipher_close(ghandle);
}

static void aes_block_start(ESP32C3AesState *s, const uint32_t *key, const uint32_t *text_in, uint32_t *text_out, const uint32_t mode_reg)
{
    AES_KEY aes_key_handle;

    /* Check whether we have to encrypt or decrypt */
    const uint32_t mode = FIELD_EX32(mode_reg, AES_MODE_REG, AES_MODE);
    const bool encrypt = (mode == ESP32C3_AES_MODE_128_ENC) || (mode == ESP32C3_AES_MODE_256_ENC);
    const bool decrypt = (mode == ESP32C3_AES_MODE_128_DEC) || (mode == ESP32C3_AES_MODE_256_DEC);

    /* Get the length, in bits of the key */
    const int length = (mode == ESP32C3_AES_MODE_128_ENC || mode == ESP32C3_AES_MODE_128_DEC) ? 128 : 256;

    /* Cast the keys and data to byte array.
     * This can only work as-is if the host computer is has a little-endian CPU.  */
    const uint8_t *aes_key = (uint8_t*) key;
    const uint8_t *tin = (uint8_t*) text_in;
    uint8_t *tout = (uint8_t*) text_out;

    if (encrypt) {
        AES_set_encrypt_key(aes_key, length, &aes_key_handle);
        AES_encrypt(tin, tout, &aes_key_handle);
    } else if (decrypt) {
        AES_set_decrypt_key(aes_key, length, &aes_key_handle);
        AES_decrypt(tin, tout, &aes_key_handle);
    }

    s->state_reg = ESP32C3_AES_IDLE;
}

static uint64_t esp32c3_aes_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3AesState *s = ESP32C3_AES(opaque);
    uint64_t r = 0;

    /* At the moment, make the assumption that we always write a 32-bit word, except for IV memory */
    assert((addr >= A_AES_IV_MEM_0_REG && addr <= A_AES_IV_MEM_15_REG) ||
            size == sizeof(uint32_t));

    switch (addr) {
    case A_AES_KEY_0_REG ... A_AES_KEY_7_REG:
        r = s->key[(addr - A_AES_KEY_0_REG) / sizeof(uint32_t)];
        break;

    case A_AES_TEXT_IN_0_REG ... A_AES_TEXT_IN_3_REG:
        r = s->text_in[(addr - A_AES_TEXT_IN_0_REG) / sizeof(uint32_t)];
        break;

    case A_AES_TEXT_OUT_0_REG ... A_AES_TEXT_OUT_3_REG:
        r = s->text_out[(addr - A_AES_TEXT_OUT_0_REG) / sizeof(uint32_t)];
        break;

    case A_AES_IV_MEM_0_REG ... A_AES_IV_MEM_15_REG:
        /* Use r as the offset */
        r = addr - A_AES_IV_MEM_0_REG;
        if (size == sizeof(uint32_t)) {
            r = *((uint32_t*) (s->iv_mem + r));
        } else if (size == sizeof(uint8_t)) {
            r = s->iv_mem[r];
        }
        break;

    case A_AES_STATE_REG:
        r = s->state_reg;
        break;

    case A_AES_MODE_REG:
        r = s->mode_reg;
        break;

    case A_AES_DMA_ENA_REG:
        r = s->dma_enable_reg;
        break;

    case A_AES_BLK_MODE_REG:
        r = s->block_mode_reg;
        break;

    case A_AES_BLK_NUM_REG:
        r = s->block_num_reg;
        break;

    case A_AES_INC_SEL_REG:
        r = s->inc_sel_reg;
        break;

    case A_AES_INT_ENA_REG:
        r = s->int_ena_reg;
        break;

    default:
#if AES_WARNING
        /* Other registers are not supported yet */
        warn_report("[AES] Unsupported read to %08lx", addr);
#endif
        break;
    }

#if AES_DEBUG
    info_report("[AES] Reading from %08lx (%08lx)", addr, r);
#endif


    return r;
}


static void esp32c3_aes_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    ESP32C3AesClass *class = ESP32C3_AES_GET_CLASS(opaque);
    ESP32C3AesState *s = ESP32C3_AES(opaque);
    uint32_t offset = 0;

    /* At the moment, make the assumption that we always write a 32-bit word, except for IV memory */
    assert((addr >= A_AES_IV_MEM_0_REG && addr <= A_AES_IV_MEM_15_REG) ||
            size == sizeof(uint32_t));

    switch (addr) {
    case A_AES_KEY_0_REG ... A_AES_KEY_7_REG:
        s->key[(addr - A_AES_KEY_0_REG) / sizeof(uint32_t)] = value;
        break;

    case A_AES_TEXT_IN_0_REG ... A_AES_TEXT_IN_3_REG:
        s->text_in[(addr - A_AES_TEXT_IN_0_REG) / sizeof(uint32_t)] = value;
        break;

    case A_AES_IV_MEM_0_REG ... A_AES_IV_MEM_15_REG:
        offset = addr - A_AES_IV_MEM_0_REG;
        if (size == sizeof(uint32_t)) {
            *((uint32_t*) (s->iv_mem + offset)) = value;
        } else if (size == sizeof(uint8_t)) {
            s->iv_mem[offset] = value & 0xff;
        }
        break;

    case A_AES_MODE_REG:
        s->mode_reg = value;
        break;

    case A_AES_TRIGGER_REG:
        if (FIELD_EX32(value, AES_TRIGGER_REG, AES_TRIGGER)) {
            /* DMA mode is different than "regular" mode */
            if (FIELD_EX32(s->dma_enable_reg , AES_DMA_ENA_REG, AES_DMA_ENA) != 0) {
                esp32c3_aes_dma_start(s);
            } else {
                class->aes_block_start(s, s->key, s->text_in, s->text_out, s->mode_reg);
            }
        }
        break;

    case A_AES_DMA_ENA_REG:
        s->dma_enable_reg = value;
        break;

    case A_AES_BLK_MODE_REG:
        s->block_mode_reg = value;
        break;

    case A_AES_BLK_NUM_REG:
        s->block_num_reg = value;
        break;

    case A_AES_INC_SEL_REG:
        s->inc_sel_reg = value;
        break;

    case A_AES_INT_CLR_REG:
        if (FIELD_EX32(value, AES_INT_CLR_REG, AES_INT_CLR)) {
            qemu_irq_lower(s->irq);
        }
        break;

    case A_AES_INT_ENA_REG:
        s->int_ena_reg = FIELD_EX32(value, AES_INT_ENA_REG, AES_INT_ENA) ? 1 : 0;
        break;

    case A_AES_DMA_EXIT_REG:
        esp32c3_aes_dma_exit(s);
        break;

    default:
#if AES_WARNING
        /* Other registers are not supported yet */
        warn_report("[AES] Unsupported write to %08lx (%08lx)", addr, value);
#endif
        break;
    }

#if AES_DEBUG
    info_report("[AES] Writing to %08lx (%08lx)", addr, value);
#endif

}

static const MemoryRegionOps esp32c3_aes_ops = {
        .read =  esp32c3_aes_read,
        .write = esp32c3_aes_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_aes_reset(DeviceState *dev)
{
    ESP32C3AesState *s = ESP32C3_AES(dev);
    memset(s->key, 0, ESP32C3_AES_KEY_REG_CNT * sizeof(uint32_t));
    memset(s->text_in, 0, ESP32C3_AES_TEXT_REG_CNT * sizeof(uint32_t));
    memset(s->text_out, 0, ESP32C3_AES_TEXT_REG_CNT * sizeof(uint32_t));

    s->state_reg = ESP32C3_AES_IDLE;
    s->mode_reg = 0;
    s->dma_enable_reg = 0;
    s->block_mode_reg = 0;
    s->block_num_reg = 0;
    s->inc_sel_reg = 0;
    s->int_ena_reg = 0;
}

static void esp32c3_aes_realize(DeviceState *dev, Error **errp)
{
    ESP32C3AesState *s = ESP32C3_AES(dev);

    /* Make sure GDMA was set of issue an error */
    if (s->gdma == NULL) {
        error_report("[AES] GDMA controller must be set!");
    }
}

static void esp32c3_aes_init(Object *obj)
{
    ESP32C3AesState *s = ESP32C3_AES(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_aes_ops, s,
                          TYPE_ESP32C3_AES, ESP32C3_AES_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);
}

static void esp32c3_aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32C3AesClass* esp32c3_aes = ESP32C3_AES_CLASS(klass);

    dc->realize = esp32c3_aes_realize;
    dc->reset = esp32c3_aes_reset;

    esp32c3_aes->aes_block_start = aes_block_start;
}

static const TypeInfo esp32c3_aes_info = {
        .name = TYPE_ESP32C3_AES,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(ESP32C3AesState),
        .instance_init = esp32c3_aes_init,
        .class_init = esp32c3_aes_class_init,
        .class_size = sizeof(ESP32C3AesClass)
};

static void esp32c3_aes_register_types(void)
{
    type_register_static(&esp32c3_aes_info);
}

type_init(esp32c3_aes_register_types)

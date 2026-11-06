/*
 * ESP AES emulation
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/misc/esp_aes.h"
#include "qemu/error-report.h"
#include <gcrypt.h>
#include "qemu/bswap.h"
#include "hw/irq.h"
#include "crypto/aes.h"

#define AES_WARNING 0
#define AES_DEBUG   0


static void esp_aes_dma_exit(ESPAesState *s)
{
    s->state_reg = ESP_AES_IDLE;
}

static void* esp_aes_get_buffer(uint32_t size)
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
static void esp_aes_ctr_add_counter(ESPAesState *s, uint32_t blocks)
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
        uint32_t *counter_ptr = (uint32_t*) &s->iv_mem[ESP_AES_IV_REG_CNT - sizeof(uint32_t)];
        const uint32_t value = be32_to_cpu(*counter_ptr) + blocks;
        *counter_ptr = cpu_to_be32(value);
    }
}


static void esp_aes_dma_start(ESPAesState *s)
{
    gcry_cipher_hd_t ghandle;
    uint32_t gdma_out_idx;
    uint32_t gdma_in_idx;

    const enum gcry_cipher_modes cipher_map[ESP_AES_CIPHER_COUNT] = {
        [ESP_AES_ECB_CIPHER]    = GCRY_CIPHER_MODE_ECB,
        [ESP_AES_CBC_CIPHER]    = GCRY_CIPHER_MODE_CBC,
        [ESP_AES_OFB_CIPHER]    = GCRY_CIPHER_MODE_OFB,
        [ESP_AES_CTR_CIPHER]    = GCRY_CIPHER_MODE_CTR,
        [ESP_AES_CFB8_CIPHER]   = GCRY_CIPHER_MODE_CFB8,
        [ESP_AES_CFB128_CIPHER] = GCRY_CIPHER_MODE_CFB,
    };

    /* Get the block Cipher mode */
    const uint32_t cipher_mode = FIELD_EX32(s->block_mode_reg , AES_BLK_MODE_REG, AES_BLOCK_MODE);

    /* Check whether we have to encrypt or decrypt */
    const uint32_t mode = FIELD_EX32(s->mode_reg , AES_MODE_REG, AES_MODE);
    const bool encrypt = (mode == ESP_AES_MODE_128_ENC) || (mode == ESP_AES_MODE_256_ENC);
    const bool decrypt = (mode == ESP_AES_MODE_128_DEC) || (mode == ESP_AES_MODE_256_DEC);

    /* Get the length, in bits of the key */
    const int length = (mode == ESP_AES_MODE_128_ENC || mode == ESP_AES_MODE_128_DEC) ? 128 : 256;
    const int algo = length == 128 ? GCRY_CIPHER_AES128 : GCRY_CIPHER_AES256;

    if (cipher_mode >= ESP_AES_CIPHER_COUNT) {
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
    if (cipher_mode == ESP_AES_CTR_CIPHER) {
        err = gcry_cipher_setctr(ghandle, iv_mem, ESP_AES_IV_REG_CNT);
    } else if (cipher_mode != ESP_AES_ECB_CIPHER) {
        err = gcry_cipher_setiv(ghandle, iv_mem, ESP_AES_IV_REG_CNT);
    }

    if (err) {
        error_report("[AES] error 0x%x setting IV memory", err);
        goto close_exit;
    }

    /* Get the GDMA input channel index for AES peripheral */
    assert(s->gdma != NULL);

    if ( !esp_gdma_get_channel_periph(s->gdma, GDMA_AES, ESP_GDMA_OUT_IDX, &gdma_out_idx) ||
         !esp_gdma_get_channel_periph(s->gdma, GDMA_AES, ESP_GDMA_IN_IDX, &gdma_in_idx) ) {
        warn_report("[AES] GDMA requested but no properly configured channel found");
        goto close_exit;
    }

    /* Block number represents the number of 128-bit (16-byte) blocks to encrypt.
     * If block_num_reg is 100, we have to encrypt 100*128/8 = 1600 bytes */
    uint32_t buf_size = s->block_num_reg * 16;
    uint8_t *buffer = esp_aes_get_buffer(buf_size);

    if ( !esp_gdma_read_channel(s->gdma, gdma_out_idx, buffer, buf_size) ) {
        warn_report("[AES] Error reading from GDMA buffer");
        goto close_exit;
    }

    /* Reading was successful, process the buffer (encrypt/decrypt) and write back to the GDMA OUT buffer */
    if (encrypt) {
        err = gcry_cipher_encrypt(ghandle, buffer, buf_size, NULL, 0);

        if (cipher_mode != ESP_AES_CTR_CIPHER) {
            /* On the real hardware, IV memory is used in-place for encrypting data, so copy the last encrypted block to IV memory */
            memcpy(iv_mem, buffer + buf_size - 16, ESP_AES_IV_REG_CNT);
        }
    } else {
        /* Store the last block of plaintext, needed for OFB */
        const uint8_t *buffer_last_block = buffer + buf_size - ESP_AES_IV_REG_CNT;
        uint8_t plaintext[ESP_AES_IV_REG_CNT];
        memcpy(plaintext, buffer_last_block, ESP_AES_IV_REG_CNT);

        /* The IV memory is initalized with the encrypted data, so do the copy now */
        if (cipher_mode != ESP_AES_OFB_CIPHER && cipher_mode != ESP_AES_CTR_CIPHER) {
            memcpy(iv_mem, buffer + buf_size - 16, ESP_AES_IV_REG_CNT);
        }

        err = gcry_cipher_decrypt(ghandle, buffer, buf_size, NULL, 0);

        /* For OFB, it is done after the decryption. Moreover, the hardware XOR the original plaintext with the output
         * and stores the result in IV memory. */
        if (cipher_mode == ESP_AES_OFB_CIPHER) {
            for (int i = 0; i < ESP_AES_IV_REG_CNT; i++) {
                iv_mem[i] = buffer_last_block[i] ^ plaintext[i];
            }
        }
    }

    if (cipher_mode == ESP_AES_CTR_CIPHER) {
        esp_aes_ctr_add_counter(s, s->block_num_reg);
    }

    if (err) {
        error_report("[AES] error processing memory");
        goto close_exit;
    }

    if ( !esp_gdma_write_channel(s->gdma, gdma_in_idx, buffer, buf_size) ) {
        warn_report("[AES] Error writing to GDMA buffer");
        goto close_exit;
    }

    s->state_reg = ESP_AES_DONE;

    if (s->int_ena_reg) {
        qemu_irq_raise(s->irq);
    }

close_exit:
    gcry_cipher_close(ghandle);
}

/* Encrypts whole blocks in place with the key of this operation. GCM never uses the AES decrypt direction. */
static bool esp_aes_gcm_ecb(ESPAesState *s, uint8_t *blocks, size_t len)
{
    const uint32_t mode = FIELD_EX32(s->mode_reg, AES_MODE_REG, AES_MODE);
    const bool is_128 = mode == ESP_AES_MODE_128_ENC || mode == ESP_AES_MODE_128_DEC;
    gcry_cipher_hd_t handle;
    gcry_error_t err;

    err = gcry_cipher_open(&handle, is_128 ? GCRY_CIPHER_AES128 : GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
    if (err) {
        error_report("[AES] error 0x%x when opening cipher", err);
        return false;
    }
    err = gcry_cipher_setkey(handle, s->key, is_128 ? 16 : 32);
    if (!err) {
        err = gcry_cipher_encrypt(handle, blocks, len, NULL, 0);
    }
    gcry_cipher_close(handle);
    return err == 0;
}

/* Multiplies y by the hash key in GF(2^128) after adding a block, as GHASH defines. */
static void esp_aes_ghash_block(uint8_t *y, const uint8_t *h, const uint8_t *block)
{
    uint64_t vh = ldq_be_p(h), vl = ldq_be_p(h + 8);
    uint64_t zh = 0, zl = 0;

    for (int i = 0; i < ESP_AES_BLOCK_SIZE; i++) {
        y[i] ^= block[i];
    }
    for (int i = 0; i < 128; i++) {
        if (y[i / 8] & (0x80 >> (i % 8))) {
            zh ^= vh;
            zl ^= vl;
        }
        const bool lsb = vl & 1;
        vl = (vl >> 1) | (vh << 63);
        vh >>= 1;
        if (lsb) {
            vh ^= 0xE100000000000000ULL;
        }
    }
    stq_be_p(y, zh);
    stq_be_p(y + 8, zl);
}

static void esp_aes_gcm_done(ESPAesState *s)
{
    s->state_reg = ESP_AES_DONE;
    if (s->int_ena_reg) {
        qemu_irq_raise(s->irq);
    }
}

/*
 * Runs a GCM operation. The DMA input has the AAD blocks, the data blocks and the
 * length block. The DMA output has only the data blocks. The guest pads a partial
 * last block with zeros, and REMAINDER_BIT_NUM gives its size.
 */
static void esp_aes_gcm_continue(ESPAesState *s)
{
    const uint32_t mode = FIELD_EX32(s->mode_reg, AES_MODE_REG, AES_MODE);
    const bool encrypt = mode == ESP_AES_MODE_128_ENC || mode == ESP_AES_MODE_256_ENC;
    const size_t aad_len = s->aad_block_num_reg * ESP_AES_BLOCK_SIZE;
    const size_t data_len = s->block_num_reg * ESP_AES_BLOCK_SIZE;
    const size_t total = aad_len + data_len + ESP_AES_BLOCK_SIZE;
    const uint32_t remainder = s->remainder_bit_num_reg / 8;
    uint8_t *input = g_malloc(total);
    uint8_t *data = input + aad_len;
    uint8_t *stream = g_malloc(data_len + ESP_AES_BLOCK_SIZE);
    uint8_t y[ESP_AES_BLOCK_SIZE] = { 0 };
    uint32_t out_chan, in_chan;

    if (!esp_gdma_get_channel_periph(s->gdma, GDMA_AES, ESP_GDMA_OUT_IDX, &out_chan) ||
        !esp_gdma_get_channel_periph(s->gdma, GDMA_AES, ESP_GDMA_IN_IDX, &in_chan)) {
        warn_report("[AES] GDMA requested but no properly configured channel found");
        goto exit;
    }
    if (!esp_gdma_read_channel(s->gdma, out_chan, input, total)) {
        warn_report("[AES] Error reading from GDMA buffer");
        goto exit;
    }

    /* The stream starts with E(J0) for the tag, then E(inc32(J0)) and so on */
    const uint32_t counter = ldl_be_p(s->gcm_j0 + 12);
    for (size_t i = 0; i <= s->block_num_reg; i++) {
        memcpy(stream + i * ESP_AES_BLOCK_SIZE, s->gcm_j0, 12);
        stl_be_p(stream + i * ESP_AES_BLOCK_SIZE + 12, counter + i);
    }
    if (!esp_aes_gcm_ecb(s, stream, data_len + ESP_AES_BLOCK_SIZE)) {
        goto exit;
    }

    for (size_t i = 0; i < aad_len; i += ESP_AES_BLOCK_SIZE) {
        esp_aes_ghash_block(y, s->gcm_h, input + i);
    }
    for (size_t i = 0; i < data_len; i += ESP_AES_BLOCK_SIZE) {
        uint8_t ciphertext[ESP_AES_BLOCK_SIZE];

        for (int j = 0; j < ESP_AES_BLOCK_SIZE; j++) {
            const uint8_t in = data[i + j];
            data[i + j] = in ^ stream[ESP_AES_BLOCK_SIZE + i + j];
            ciphertext[j] = encrypt ? data[i + j] : in;
        }
        if (remainder && i + ESP_AES_BLOCK_SIZE == data_len) {
            memset(ciphertext + remainder, 0, ESP_AES_BLOCK_SIZE - remainder);
        }
        esp_aes_ghash_block(y, s->gcm_h, ciphertext);
    }
    esp_aes_ghash_block(y, s->gcm_h, data + data_len);
    for (int j = 0; j < ESP_AES_BLOCK_SIZE; j++) {
        s->gcm_t0[j] = y[j] ^ stream[j];
    }

    if (data_len && !esp_gdma_write_channel(s->gdma, in_chan, data, data_len)) {
        warn_report("[AES] Error writing to GDMA buffer");
        goto exit;
    }
    esp_aes_gcm_done(s);

exit:
    g_free(stream);
    g_free(input);
}

/* Returns the GCM memory register at addr, or NULL if this chip has none there. */
static uint32_t *esp_aes_gcm_reg(ESPAesState *s, hwaddr addr)
{
    if (!ESP_AES_GET_CLASS(s)->gcm || (addr & 3)) {
        return NULL;
    }
    if (addr >= A_AES_H_MEM && addr < A_AES_H_MEM + ESP_AES_BLOCK_SIZE) {
        return (uint32_t *)(s->gcm_h + addr - A_AES_H_MEM);
    } else if (addr >= A_AES_J0_MEM && addr < A_AES_J0_MEM + ESP_AES_BLOCK_SIZE) {
        return (uint32_t *)(s->gcm_j0 + addr - A_AES_J0_MEM);
    } else if (addr >= A_AES_T0_MEM && addr < A_AES_T0_MEM + ESP_AES_BLOCK_SIZE) {
        return (uint32_t *)(s->gcm_t0 + addr - A_AES_T0_MEM);
    } else if (addr == A_AES_AAD_BLOCK_NUM_REG) {
        return &s->aad_block_num_reg;
    } else if (addr == A_AES_REMAINDER_BIT_NUM_REG) {
        return &s->remainder_bit_num_reg;
    }
    return NULL;
}

static void aes_block_start(ESPAesState *s, const uint32_t *key, const uint32_t *text_in, uint32_t *text_out, const uint32_t mode_reg)
{
    AES_KEY aes_key_handle;

    /* Check whether we have to encrypt or decrypt */
    const uint32_t mode = FIELD_EX32(mode_reg, AES_MODE_REG, AES_MODE);
    const bool encrypt = (mode == ESP_AES_MODE_128_ENC) || (mode == ESP_AES_MODE_256_ENC);
    const bool decrypt = (mode == ESP_AES_MODE_128_DEC) || (mode == ESP_AES_MODE_256_DEC);

    /* Get the length, in bits of the key */
    const int length = (mode == ESP_AES_MODE_128_ENC || mode == ESP_AES_MODE_128_DEC) ? 128 : 256;

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

    s->state_reg = ESP_AES_IDLE;
}

static uint64_t esp_aes_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPAesState *s = ESP_AES(opaque);
    uint64_t r = 0;
    uint32_t *gcm_reg = esp_aes_gcm_reg(s, addr);

    /* At the moment, make the assumption that we always write a 32-bit word, except for IV memory */
    assert((addr >= A_AES_IV_MEM_0_REG && addr <= A_AES_IV_MEM_15_REG) ||
            size == sizeof(uint32_t));

    if (gcm_reg) {
        return *gcm_reg;
    }

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


static void esp_aes_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    ESPAesClass *class = ESP_AES_GET_CLASS(opaque);
    ESPAesState *s = ESP_AES(opaque);
    uint32_t offset = 0;
    uint32_t *gcm_reg = esp_aes_gcm_reg(s, addr);

    /* At the moment, make the assumption that we always write a 32-bit word, except for IV memory */
    assert((addr >= A_AES_IV_MEM_0_REG && addr <= A_AES_IV_MEM_15_REG) ||
            size == sizeof(uint32_t));

    if (gcm_reg) {
        *gcm_reg = value;
        return;
    }

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
                if (class->gcm && FIELD_EX32(s->block_mode_reg, AES_BLK_MODE_REG, AES_BLOCK_MODE) == ESP_AES_GCM_CIPHER) {
                    /* This trigger computes H. CONTINUE starts the transform. */
                    memset(s->gcm_h, 0, sizeof(s->gcm_h));
                    esp_aes_gcm_ecb(s, s->gcm_h, sizeof(s->gcm_h));
                    s->state_reg = ESP_AES_IDLE;
                } else {
                    esp_aes_dma_start(s);
                }
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

    case A_AES_CONTINUE_REG:
        if (class->gcm && (value & 1)) {
            esp_aes_gcm_continue(s);
        }
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
        esp_aes_dma_exit(s);
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

static const MemoryRegionOps esp_aes_ops = {
        .read =  esp_aes_read,
        .write = esp_aes_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp_aes_reset_hold(Object *obj, ResetType type)
{
    ESPAesState *s = ESP_AES(obj);
    memset(s->key, 0, ESP_AES_KEY_REG_CNT * sizeof(uint32_t));
    memset(s->text_in, 0, ESP_AES_TEXT_REG_CNT * sizeof(uint32_t));
    memset(s->text_out, 0, ESP_AES_TEXT_REG_CNT * sizeof(uint32_t));

    s->state_reg = ESP_AES_IDLE;
    s->mode_reg = 0;
    s->dma_enable_reg = 0;
    s->block_mode_reg = 0;
    s->block_num_reg = 0;
    s->inc_sel_reg = 0;
    s->int_ena_reg = 0;
    memset(s->gcm_h, 0, sizeof(s->gcm_h));
    memset(s->gcm_j0, 0, sizeof(s->gcm_j0));
    memset(s->gcm_t0, 0, sizeof(s->gcm_t0));
    s->aad_block_num_reg = 0;
    s->remainder_bit_num_reg = 0;
}

static void esp_aes_realize(DeviceState *dev, Error **errp)
{
    ESPAesState *s = ESP_AES(dev);

    /* Make sure GDMA was set of issue an error */
    if (s->gdma == NULL) {
        error_report("[AES] GDMA controller must be set!");
    }
}

static void esp_aes_init(Object *obj)
{
    ESPAesState *s = ESP_AES(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_aes_ops, s,
                          TYPE_ESP_AES, ESP_AES_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);
}

static void esp_aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESPAesClass* esp_aes = ESP_AES_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp_aes_realize;
    rc->phases.hold = esp_aes_reset_hold;

    esp_aes->aes_block_start = aes_block_start;
}

static const TypeInfo esp_aes_info = {
    .name = TYPE_ESP_AES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPAesState),
    .instance_init = esp_aes_init,
    .class_init = esp_aes_class_init,
    .class_size = sizeof(ESPAesClass),
    .abstract = true
};

static void esp_aes_register_types(void)
{
    type_register_static(&esp_aes_info);
}

type_init(esp_aes_register_types)

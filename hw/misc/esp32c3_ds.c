/*
 * ESP32-C3 Digital Signature emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "hw/misc/esp32c3_ds.h"

#define DS_WARNING 0
#define DS_DEBUG   0

static void write_and_padd(uint8_t *block, const uint8_t *data, uint16_t data_len)
{
    memcpy(block, data, data_len);
    // Apply a one bit, followed by zero bits (refer to the TRM of respective target).
    block[data_len] = 0x80;
    memset(block + data_len + 1, 0, SHA256_BLOCK_SIZE - data_len - 1);
}


static void esp32c3_ds_generate_ds_key(ESP32C3DsState *s)
{
    uint8_t ones[ESP32C3_DS_KEY_SIZE];
    memset(ones, 0xFF, ESP32C3_DS_KEY_SIZE);

    uint8_t block[SHA256_BLOCK_SIZE];
    uint64_t bit_len = be64_to_cpu(sizeof(ones) * 8 + 512);

    write_and_padd(block, ones, sizeof(ones));
    memcpy(block + SHA256_BLOCK_SIZE - sizeof(bit_len), &bit_len, sizeof(bit_len));

    ESP32C3HmacClass *hmac_class = ESP32C3_HMAC_GET_CLASS(s->hmac);

    hmac_class->hmac_update(s->hmac, (uint32_t*) block);
    hmac_class->hmac_finish(s->hmac, s->ds_key);

    for(int i = 0; i < ESP32C3_DS_KEY_SIZE / 4; i++) {
        s->ds_key[i] = be32_to_cpu(s->ds_key[i]);
    }

    // The DS peripheral resets the HMAC peripheral once it has completed the respective operation.
    DeviceClass *hmac_dc = DEVICE_CLASS(hmac_class);
    hmac_dc->reset((DeviceState*)(s->hmac));
}


static void esp32c3_ds_decrypt_ciphertext(ESP32C3DsState *s, uint8_t *buffer)
{
    ESP32C3AesClass *aes_class = ESP32C3_AES_GET_CLASS(s->aes);

    uint32_t *output_words = (uint32_t *)buffer;
    const uint32_t *input_words = (const uint32_t *)buffer;

    uint32_t iv_words[ESP32C3_DS_IV_SIZE / 4];
    memcpy(iv_words, s->iv, ESP32C3_DS_IV_SIZE);

    unsigned char temp[16];
    uint32_t length = ESP32C3_DS_CIPHERTEXT_SIZE;

    while ( length > 0 ) {
        memcpy(temp, input_words, 16);

        aes_class->aes_block_start(s->aes, s->ds_key, input_words, output_words, ESP32C3_AES_MODE_256_DEC);

        output_words[0] = output_words[0] ^ iv_words[0];
        output_words[1] = output_words[1] ^ iv_words[1];
        output_words[2] = output_words[2] ^ iv_words[2];
        output_words[3] = output_words[3] ^ iv_words[3];

        memcpy( iv_words, temp, 16 );

        input_words += 4;
        output_words += 4;
        length -= 16;
    }

    // The DS peripheral resets the AES peripheral once it has completed the respective operation.
    DeviceClass *aes_dc = DEVICE_CLASS(aes_class);
    aes_dc->reset((DeviceState*)(s->aes));
}


static bool md_and_pad_check(ESP32C3DsState *s)
{
    /* parse box */
    int index = 0;
    bool ret = false;

    /* md */
    uint8_t md[ESP32C3_DS_MD_SIZE];
    memcpy(md, s->box_mem + index, ESP32C3_DS_MD_SIZE);
    index += ESP32C3_DS_MD_SIZE / 4;

    /* mprime */
    uint8_t mprime[ESP32C3_DS_MPRIME_SIZE];
    memcpy(mprime, s->box_mem + index, ESP32C3_DS_MPRIME_SIZE);
    index += ESP32C3_DS_MPRIME_SIZE / 4;

    /* l */
    uint8_t l[ESP32C3_DS_L_SIZE];
    memcpy(l, s->box_mem + index, ESP32C3_DS_L_SIZE);
    index += ESP32C3_DS_L_SIZE / 4;

    /* beta */
    uint8_t beta[8];
    memcpy(beta, s->box_mem + index, 8);

    /* Padding check */
    uint8_t beta_pkcs7[8];
    memset(beta_pkcs7, 8, sizeof(beta_pkcs7));

    s->ds_signature_check = DS_SIGNATURE_PADDING_AND_MD_FAIL;

    if (memcmp(beta_pkcs7, beta, sizeof(beta_pkcs7)) == 0) {
        s->ds_signature_check ^= DS_SIGNATURE_PADDING_FAIL;
    } else {
        error_report("[Digital Signature] Invalid padding");
    }

    /* MD check */
    uint32_t md_check[ESP32C3_DS_MD_SIZE / 4];
    ESP32C3ShaClass *sha_class = ESP32C3_SHA_GET_CLASS(s->sha);

    uint8_t buffer[ESP32C3_DS_CALC_MD_SIZE];
    index = 0;

    memcpy(buffer + index, s->y_mem, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    memcpy(buffer + index, s->m_mem, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    memcpy(buffer + index, s->rb_mem, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    memcpy(buffer + index, mprime, ESP32C3_DS_MPRIME_SIZE);
    index += ESP32C3_DS_MPRIME_SIZE;

    memcpy(buffer + index, l, ESP32C3_DS_L_SIZE);
    index += ESP32C3_DS_L_SIZE;

    memcpy(buffer + index, s->iv, ESP32C3_DS_IV_SIZE);

    size_t remaining_blocks = ESP32C3_DS_CALC_MD_SIZE / SHA256_BLOCK_SIZE;

    for (int i = 0; i < remaining_blocks; i++) {
        if (i == 0) {
            sha_class->sha_start(s->sha, OP_START, ESP32C3_SHA_256_MODE, (uint32_t*) (buffer + i * SHA256_BLOCK_SIZE), md_check);
        } else {
            sha_class->sha_start(s->sha, OP_CONTINUE, ESP32C3_SHA_256_MODE, (uint32_t*) (buffer + i * SHA256_BLOCK_SIZE), md_check);
        }
    }

    size_t remaining = ESP32C3_DS_CALC_MD_SIZE % SHA256_BLOCK_SIZE;
    if (remaining != 0) {
        uint8_t block[SHA256_BLOCK_SIZE];
        uint64_t bit_len = be64_to_cpu(ESP32C3_DS_CALC_MD_SIZE * 8);
        write_and_padd(block, buffer + ESP32C3_DS_CALC_MD_SIZE - remaining, remaining);
        memcpy(block + SHA256_BLOCK_SIZE - sizeof(bit_len), &bit_len, sizeof(bit_len));
        sha_class->sha_start(s->sha, OP_CONTINUE, ESP32C3_SHA_256_MODE, (uint32_t*) block, md_check);
    }

    for (int i = 0; i < SHA256_DIGEST_SIZE / 4; i++) {
        md_check[i] = be32_to_cpu(md_check[i]);
    }

    // The DS peripheral resets the SHA peripheral once it has completed the respective operation.
    DeviceClass *sha_dc = DEVICE_CLASS(sha_class);
    sha_dc->reset((DeviceState*)(s->sha));

    if (memcmp(md, md_check, SHA256_DIGEST_SIZE) == 0) {
        s->ds_signature_check ^= DS_SIGNATURE_MD_FAIL;
        ret = true;
    } else {
        error_report("[Digital Signature] Invalid digest");
    }
    return ret;
}


static void esp32c3_ds_generate_signature(ESP32C3DsState *s)
{
    uint32_t mode = ESP32C3_DS_MEM_BLK_SIZE / 4 - 1;

    ESP32C3RsaClass *rsa_class = ESP32C3_RSA_GET_CLASS(s->rsa);
    rsa_class->rsa_exp_mod(s->rsa, mode, s->x_mem, s->y_mem, s->m_mem, s->z_mem, 0);

    // The DS peripheral resets the RSA peripheral once it has completed the respective operation.
    DeviceClass *rsa_dc = DEVICE_CLASS(rsa_class);
    rsa_dc->reset((DeviceState*)(s->rsa));
}


static void esp32c3_ds_calculate(ESP32C3DsState *s)
{
    /* Re-Generate the plaintext from the ciphertext */
    uint8_t buffer[ESP32C3_DS_CIPHERTEXT_SIZE];
    int index = 0;
    /* Y */
    memcpy(buffer + index, s->y_mem, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    /* M */
    memcpy(buffer + index, s->m_mem, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    /* rb */
    memcpy(buffer + index, s->rb_mem, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    /* box */
    memcpy(buffer + index, s->box_mem, ESP32C3_DS_BOX_MEM_BLK_SIZE);

    /* Decrypt ciphertext */
    esp32c3_ds_decrypt_ciphertext(s, buffer);

    /* Parse params */
    /* Y */
    index = 0;
    memcpy(s->y_mem, buffer + index, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    /* M */
    memcpy(s->m_mem, buffer + index, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    /* rb */
    memcpy(s->rb_mem, buffer + index, ESP32C3_DS_MEM_BLK_SIZE);
    index += ESP32C3_DS_MEM_BLK_SIZE;

    /* box */
    memcpy(s->box_mem, buffer + index, ESP32C3_DS_BOX_MEM_BLK_SIZE);

    if (!md_and_pad_check(s)) {
        return;
    }

    /* Generate signature */
    esp32c3_ds_generate_signature(s);
}


static void esp32c3_ds_clear_buffers(ESP32C3DsState *s)
{
    memset(s->y_mem, 0, ESP32C3_DS_MEM_BLK_SIZE);
    memset(s->m_mem, 0, ESP32C3_DS_MEM_BLK_SIZE);
    memset(s->rb_mem, 0, ESP32C3_DS_MEM_BLK_SIZE);
    memset(s->box_mem, 0, ESP32C3_DS_BOX_MEM_BLK_SIZE);
    memset(s->x_mem, 0, ESP32C3_DS_MEM_BLK_SIZE);
    memset(s->z_mem, 0, ESP32C3_DS_MEM_BLK_SIZE);
    memset(s->iv, 0, ESP32C3_DS_IV_SIZE);
    memset(s->ds_key, 0, ESP32C3_DS_KEY_SIZE);
}


static uint64_t esp32c3_ds_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3DsState *s = ESP32C3_DS(opaque);

    uint64_t r = 0;
    switch (addr) {
        case A_DS_QUERY_BUSY_REG:
            r = 0;
            break;

        case A_DS_QUERY_CHECK_REG:
            r = s->ds_signature_check;
            break;

        case A_DS_DATE_REG:
            r = 0x20200618;
            break;

        case A_DS_MEM_Z_BLOCK_BASE ... (A_DS_MEM_Z_BLOCK_BASE + ESP32C3_DS_MEM_BLK_SIZE - 1):
            r = s->z_mem[(addr - A_DS_MEM_Z_BLOCK_BASE) / sizeof(uint32_t)];
            break;

        case A_DS_QUERY_KEY_WRONG_REG:
        default:
#if DS_WARNING
            /* Other registers are not supported yet */
            warn_report("[Digital Signature] Unsupported read to %08lx\n", addr);
#endif
            break;
    }

#if DS_DEBUG
    info_report("[Digital Signature] Reading from %08lx (%08lx)\n", addr, r);
#endif

    return r;
}


static void esp32c3_ds_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32C3DsState *s = ESP32C3_DS(opaque);

    /* Only support word aligned access for the moment */
    if (size != sizeof(uint32_t)) {
        error_report("[Digital Signature] Only 32-bit word access supported at the moment");
    }

    switch (addr) {
        case A_DS_MEM_Y_BLOCK_BASE ... (A_DS_MEM_Y_BLOCK_BASE + ESP32C3_DS_MEM_BLK_SIZE - 1):
            s->y_mem[(addr - A_DS_MEM_Y_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_M_BLOCK_BASE ... (A_DS_MEM_M_BLOCK_BASE + ESP32C3_DS_MEM_BLK_SIZE - 1):
            s->m_mem[(addr - A_DS_MEM_M_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_RB_BLOCK_BASE ... (A_DS_MEM_RB_BLOCK_BASE + ESP32C3_DS_MEM_BLK_SIZE - 1):
            s->rb_mem[(addr - A_DS_MEM_RB_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_BOX_BLOCK_BASE ... (A_DS_MEM_BOX_BLOCK_BASE + ESP32C3_DS_BOX_MEM_BLK_SIZE - 1):
            s->box_mem[(addr - A_DS_MEM_BOX_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_X_BLOCK_BASE ... (A_DS_MEM_X_BLOCK_BASE + ESP32C3_DS_MEM_BLK_SIZE - 1):
            s->x_mem[(addr - A_DS_MEM_X_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_IV_0_REG...A_DS_IV_3_REG:
            s->iv[(addr - A_DS_IV_0_REG) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_SET_START_REG:
            esp32c3_ds_generate_ds_key(s);
            break;

        case A_DS_SET_ME_REG:
            esp32c3_ds_calculate(s);
            break;

        case A_DS_SET_FINISH_REG:
            esp32c3_ds_clear_buffers(s);
            break;

        case A_DS_DATE_REG:
        default:
#if DS_WARNING
            /* Other registers are not supported yet */
            warn_report("[Digital Signature] Unsupported write to %08lx (%08lx)\n", addr, value);
#endif
            break;
    }

#if DS_DEBUG
    info_report("[Digital Signature] Writing to %08lx (%08lx)\n", addr, value);
#endif

}

static const MemoryRegionOps esp32c3_ds_ops = {
    .read =  esp32c3_ds_read,
    .write = esp32c3_ds_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_ds_reset(DeviceState *dev)
{
    ESP32C3DsState *s = ESP32C3_DS(dev);
    esp32c3_ds_clear_buffers(s);
    s->ds_signature_check = DS_SIGNATURE_PADDING_AND_MD_FAIL;
}

static void esp32c3_ds_realize(DeviceState *dev, Error **errp)
{
    ESP32C3DsState *s = ESP32C3_DS(dev);

    /* Make sure HMAC was set or issue an error */
    if (s->hmac == NULL) {
        error_report("[Digital Signature] HMAC controller must be set!");
    }

    /* Make sure AES was set or issue an error */
    if (s->aes == NULL) {
        error_report("[Digital Signature] AES controller must be set!");
    }

    /* Make sure RSA was set or issue an error */
    if (s->rsa == NULL) {
        error_report("[Digital Signature] RSA controller must be set!");
    }

    /* Make sure SHA was set or issue an error */
    if (s->sha == NULL) {
        error_report("[Digital Signature] SHA controller must be set!");
    }
}

static void esp32c3_ds_init(Object *obj)
{
    ESP32C3DsState *s = ESP32C3_DS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_ds_ops, s,
                          TYPE_ESP32C3_DS, ESP32C3_DS_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c3_ds_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32c3_ds_realize;
    dc->reset = esp32c3_ds_reset;
}

static const TypeInfo esp32c3_ds_info = {
    .name = TYPE_ESP32C3_DS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3DsState),
    .instance_init = esp32c3_ds_init,
    .class_init = esp32c3_ds_class_init
};

static void esp32c3_ds_register_types(void)
{
    type_register_static(&esp32c3_ds_info);
}

type_init(esp32c3_ds_register_types)

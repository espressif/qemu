/*
 * ESP32 flash encryption
 *
 * Copyright (c) 2020 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "crypto/cipher.h"
#include "hw/misc/esp32_flash_enc.h"
#include "hw/nvram/esp32_efuse.h"

#define FLASH_ENCRYPTION_KEY_WORDS  8
#define FLASH_ENCRYPTION_DATA_WORDS  4

static void esp32_flash_encryption_op(Esp32FlashEncryptionState *s);

static uint64_t esp32_flash_encryption_read(void *opaque, hwaddr addr,
                                            unsigned int size)
{
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(opaque);
    switch (addr) {
        case A_FLASH_ENCRYPTION_DONE:
            return s->encryption_done;
    }
    return 0;
}

static void esp32_flash_encryption_write(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned int size)
{
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(opaque);

    switch (addr) {
        case A_FLASH_ENCRYPTION_BUFFER_0 ... A_FLASH_ENCRYPTION_BUFFER_7:
            s->buffer_reg[(addr - A_FLASH_ENCRYPTION_BUFFER_0) / 4] = value;
            break;

        case A_FLASH_ENCRYPTION_START:
            if (FIELD_EX32(value, FLASH_ENCRYPTION_START, START)) {
                esp32_flash_encryption_op(s);
            }
            s->encryption_done = true;
            break;

        case A_FLASH_ENCRYPTION_ADDRESS:
            s->address_reg = value;
            break;

        case A_FLASH_ENCRYPTION_DONE:
            if (FIELD_EX32(value, FLASH_ENCRYPTION_DONE, DONE) == 0) {
                s->encryption_done = false;
            }
            break;
    }
}

static const MemoryRegionOps esp32_flash_encryption_ops = {
        .read = esp32_flash_encryption_read,
        .write = esp32_flash_encryption_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
};

bool esp32_flash_encryption_enabled(struct Esp32FlashEncryptionState *s)
{
    /* Logic from ESP32 Technical Reference Manual section 25.3.2 */
    if (s->dl_mode) {
        return s->encrypt_enable_reg && !s->dl_mode_enc_disabled;
    } else {
        return s->encrypt_enable_reg;
    }
}

bool esp32_flash_decryption_enabled(struct Esp32FlashEncryptionState *s)
{
    /* Logic from ESP32 Technical Reference Manual section 25.3.3 */
    if (s->dl_mode) {
        return s->decrypt_enable_reg && !s->dl_mode_dec_disabled;
    } else {
        return s->efuse_encrypt_enabled;
    }
}

static void esp32_flash_encryption_key_tweak(struct Esp32FlashEncryptionState *s,
                                             size_t offset, const uint32_t *in_key, uint32_t *out_key)
{
    uint32_t offset_5 = offset >> 5;
    uint32_t offset_5_8 = (offset_5) & 0xf;
    uint32_t offset_5_10 = (offset_5) & 0x3f;
    uint32_t offset_5_12 = (offset_5) & 0xff;
    uint32_t offset_5_14 = (offset_5) & 0x3ff;
    uint32_t offset_5_23 = (offset_5) & 0x7ffff;

    uint32_t key_tweak[FLASH_ENCRYPTION_KEY_WORDS] = {
            (offset_5_23 >> 6) | (offset_5_23 << 13),
            (offset_5_14 >> 3) | (offset_5_23 << 7) | (offset_5_23 << 26),
            (offset_5_23 >> 9) | (offset_5_23 << 10) | (offset_5_14 << 29),
            (offset_5_12 >> 4) | (offset_5_23 << 4) | (offset_5_23 << 23),
            (offset_5_23 >> 10) | (offset_5_23 << 9) | (offset_5_12 << 28),
            (offset_5_10 >> 3) | (offset_5_23 << 3) | (offset_5_23 << 22),
            (offset_5_23 >> 9) | (offset_5_23 << 10) | (offset_5_10 << 29),
            (offset_5_8) | (offset_5_23 << 4) | (offset_5_23 << 23)
    };

    for (size_t i = 0; i < FLASH_ENCRYPTION_KEY_WORDS; ++i) {
        out_key[i] = in_key[i] ^ bswap32(key_tweak[i]);
    }
}

static void reverse_key_byte_order(const uint32_t* src, uint32_t* dst)
{
    assert( src != dst );
    for (size_t i = 0; i < FLASH_ENCRYPTION_KEY_WORDS; ++i) {
        dst[i] = bswap32(src[FLASH_ENCRYPTION_KEY_WORDS - i - 1]);
    }
}

static void reverse_data_byte_order(const uint32_t* src, uint32_t* dst)
{
    assert( src != dst );
    for (size_t i = 0; i < FLASH_ENCRYPTION_DATA_WORDS; ++i) {
        dst[i] = bswap32(src[FLASH_ENCRYPTION_DATA_WORDS - i - 1]);
    }
}

static void esp32_flash_encryption_op(struct Esp32FlashEncryptionState *s)
{
    uint32_t tweaked_key[FLASH_ENCRYPTION_KEY_WORDS];
    uint32_t reversed_key[FLASH_ENCRYPTION_KEY_WORDS];
    uint32_t reversed_data[FLASH_ENCRYPTION_DATA_WORDS];
    uint32_t encrypted_data[FLASH_ENCRYPTION_DATA_WORDS];

    memset(s->encrypted_buffer, 0, sizeof(s->encrypted_buffer));
    reverse_key_byte_order(s->efuse_key, reversed_key);
    esp32_flash_encryption_key_tweak(s, s->address_reg, reversed_key, tweaked_key);
    QCryptoCipher *cipher = qcrypto_cipher_new(QCRYPTO_CIPHER_ALG_AES_256, QCRYPTO_CIPHER_MODE_ECB, (const uint8_t*) tweaked_key, FLASH_ENCRYPTION_KEY_WORDS * 4, &error_abort);
    for (size_t total_words = 0; total_words < ARRAY_SIZE(s->buffer_reg); total_words += FLASH_ENCRYPTION_DATA_WORDS) {
        reverse_data_byte_order(s->buffer_reg + total_words, reversed_data);
        qcrypto_cipher_decrypt(cipher, reversed_data, encrypted_data, sizeof(s->buffer_reg), &error_abort);
        reverse_data_byte_order(encrypted_data, s->encrypted_buffer + total_words);
    }
    qcrypto_cipher_free(cipher);
}

void esp32_flash_encryption_get_result(struct Esp32FlashEncryptionState* s, uint32_t* dst, size_t dst_words)
{
    assert(dst_words * 4 == sizeof(s->encrypted_buffer));
    memcpy(dst, s->encrypted_buffer, sizeof(s->encrypted_buffer));
}

void esp32_flash_decrypt_inplace(struct Esp32FlashEncryptionState* s, size_t flash_addr, uint32_t* data, size_t words)
{
    assert(flash_addr % 32 == 0);
    assert(words % FLASH_ENCRYPTION_DATA_WORDS == 0);
    uint32_t tweaked_key[FLASH_ENCRYPTION_KEY_WORDS];
    uint32_t reversed_key[FLASH_ENCRYPTION_KEY_WORDS];
    uint32_t reversed_data[FLASH_ENCRYPTION_DATA_WORDS];
    uint32_t decrypted_data[FLASH_ENCRYPTION_DATA_WORDS];

    reverse_key_byte_order(s->efuse_key, reversed_key);
    for (size_t pos = 0; pos < words; pos += FLASH_ENCRYPTION_DATA_WORDS) {
        uint32_t offset = flash_addr + pos * 4;
        esp32_flash_encryption_key_tweak(s, offset, reversed_key, tweaked_key);
        QCryptoCipher *cipher = qcrypto_cipher_new(QCRYPTO_CIPHER_ALG_AES_256, QCRYPTO_CIPHER_MODE_ECB, (const uint8_t*) tweaked_key, FLASH_ENCRYPTION_KEY_WORDS * 4, &error_abort);
        reverse_data_byte_order(data + pos, reversed_data);
        qcrypto_cipher_encrypt(cipher, reversed_data, decrypted_data, sizeof(decrypted_data), &error_abort);
        reverse_data_byte_order(decrypted_data, data + pos);
        qcrypto_cipher_free(cipher);
    }
}

static void esp32_flash_encryption_on_dl_mode_change(void *opaque, int n,
                                                     int level)
{
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(opaque);
    s->dl_mode = !!level;
}

static void esp32_flash_encryption_on_enc_enable_change(void *opaque, int n,
                                                        int level)
{
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(opaque);
    s->encrypt_enable_reg = !!level;
}

static void esp32_flash_encryption_on_dec_enable_change(void *opaque, int n,
                                                        int level)
{
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(opaque);
    s->decrypt_enable_reg = !!level;
}

static void esp32_flash_encryption_on_efuse_update(void *opaque, int n,
                                                   int level)
{
    if (!level) {
        return;
    }
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(opaque);
    Esp32EfuseState *efuse = esp32_efuse_find();
    /* Odd number of bits in flash_crypt_cnt indicates that flash encryption is enabled */
    s->efuse_encrypt_enabled = (ctpop32(efuse->efuse_rd.blk0_d0.flash_crypt_cnt) % 2) == 1;
    uint32_t flash_crypt_config = efuse->efuse_rd.blk0_d5.flash_crypt_config;
    if (s->efuse_encrypt_enabled && flash_crypt_config != 0xf) {
        qemu_log("%s: Unsupported value of flash_crypt_config: 0x%x\n", __func__, flash_crypt_config);
    }
    if (efuse->efuse_rd.blk0_d6.coding_scheme != 0) {
        qemu_log("%s: Unsupported value of coding_scheme: 0x%x\n",
                 __func__, efuse->efuse_rd.blk0_d6.coding_scheme);
    }
    /* Copy the key */
    memcpy(s->efuse_key, &efuse->efuse_rd.blk1[0], sizeof(s->efuse_key));
}

static void esp32_flash_encryption_init(Object *obj)
{
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_flash_encryption_ops, s,
                          TYPE_ESP32_FLASH_ENCRYPTION,
                          A_FLASH_ENCRYPTION_DONE + 4);

    qdev_init_gpio_in_named(DEVICE(s), esp32_flash_encryption_on_dl_mode_change,
                            ESP32_FLASH_ENCRYPTION_DL_MODE_GPIO, 1);
    qdev_init_gpio_in_named(DEVICE(s),
                            esp32_flash_encryption_on_enc_enable_change,
                            ESP32_FLASH_ENCRYPTION_ENC_EN_GPIO, 1);
    qdev_init_gpio_in_named(DEVICE(s),
                            esp32_flash_encryption_on_dec_enable_change,
                            ESP32_FLASH_ENCRYPTION_DEC_EN_GPIO, 1);
    qdev_init_gpio_in_named(DEVICE(s), esp32_flash_encryption_on_efuse_update,
                            ESP32_FLASH_ENCRYPTION_EFUSE_UPDATE_GPIO, 1);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32_flash_encryption_reset(DeviceState *dev)
{
    Esp32FlashEncryptionState *s = ESP32_FLASH_ENCRYPTION(dev);
    s->encryption_done = false;
}

static void esp32_flash_encryption_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_flash_encryption_reset;
}

static const TypeInfo esp32_flash_encryption_info = {
        .name = TYPE_ESP32_FLASH_ENCRYPTION,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Esp32FlashEncryptionState),
        .instance_init = esp32_flash_encryption_init,
        .class_init = esp32_flash_encryption_class_init
};

static void esp32_flash_encryption_register_types(void)
{
    type_register_static(&esp32_flash_encryption_info);
}

type_init(esp32_flash_encryption_register_types)

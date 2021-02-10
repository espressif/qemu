#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"

#define TYPE_ESP32_FLASH_ENCRYPTION "misc.esp32.flash_encryption"
#define ESP32_FLASH_ENCRYPTION(obj) OBJECT_CHECK(Esp32FlashEncryptionState, (obj), TYPE_ESP32_FLASH_ENCRYPTION)

#define ESP32_FLASH_ENCRYPTION_DL_MODE_GPIO "dl-mode"
#define ESP32_FLASH_ENCRYPTION_ENC_EN_GPIO "flash-enc-enable"
#define ESP32_FLASH_ENCRYPTION_DEC_EN_GPIO "flash-dec-enable"
#define ESP32_FLASH_ENCRYPTION_EFUSE_UPDATE_GPIO "efuse-update"

typedef struct Esp32FlashEncryptionState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t buffer_reg[8];
    uint32_t address_reg;

    bool encryption_done;
    uint32_t encrypted_buffer[8];

    bool dl_mode;
    bool encrypt_enable_reg;  /* mirrors DPORT_SPI_ENCRYPT_ENABLE state */
    bool decrypt_enable_reg;  /* mirrors DPORT_SPI_DECRYPT_ENABLE state */
    bool efuse_encrypt_enabled;
    bool dl_mode_enc_disabled;
    bool dl_mode_dec_disabled;
    uint32_t efuse_key[8];

} Esp32FlashEncryptionState;

/* returns NULL unless there is exactly one device */
static inline Esp32FlashEncryptionState *esp32_flash_encryption_find(void)
{
    Object *o = object_resolve_path_type("", TYPE_ESP32_FLASH_ENCRYPTION, NULL);
    return o ? ESP32_FLASH_ENCRYPTION(o) : NULL;
}

bool esp32_flash_encryption_enabled(struct Esp32FlashEncryptionState* s);
bool esp32_flash_decryption_enabled(struct Esp32FlashEncryptionState* s);
void esp32_flash_encryption_get_result(struct Esp32FlashEncryptionState* s, uint32_t* dst, size_t dst_words);
void esp32_flash_decrypt_inplace(struct Esp32FlashEncryptionState* s, size_t flash_addr, uint32_t* data, size_t words);


REG32(FLASH_ENCRYPTION_BUFFER_0, 0x00)
REG32(FLASH_ENCRYPTION_BUFFER_1, 0x04)
REG32(FLASH_ENCRYPTION_BUFFER_2, 0x08)
REG32(FLASH_ENCRYPTION_BUFFER_3, 0x0C)
REG32(FLASH_ENCRYPTION_BUFFER_4, 0x10)
REG32(FLASH_ENCRYPTION_BUFFER_5, 0x14)
REG32(FLASH_ENCRYPTION_BUFFER_6, 0x18)
REG32(FLASH_ENCRYPTION_BUFFER_7, 0x1C)
REG32(FLASH_ENCRYPTION_START, 0x20)
    FIELD(FLASH_ENCRYPTION_START, START, 0, 1)

REG32(FLASH_ENCRYPTION_ADDRESS, 0x24)

REG32(FLASH_ENCRYPTION_DONE, 0x28)
    FIELD(FLASH_ENCRYPTION_DONE, DONE, 0, 1)

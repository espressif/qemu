#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32c3_reg.h"
#include "crypto/sha256_i.h"
#include "crypto/sha224_i.h"
#include "crypto/sha1_i.h"
#include "hw/dma/esp32c3_gdma.h"

#define TYPE_ESP32C3_SHA "misc.esp32c3.sha"
#define ESP32C3_SHA(obj) OBJECT_CHECK(ESP32C3ShaState, (obj), TYPE_ESP32C3_SHA)

#define ESP32C3_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3ShaClass, obj, TYPE_ESP32C3_SHA)
#define ESP32C3_SHA_CLASS(klass) OBJECT_CLASS_CHECK(ESP32C3ShaClass, klass, TYPE_ESP32C3_SHA)

#define ESP32C3_SHA_REGS_SIZE (0xC0)


/**
 * @brief Size of the message array, in bytes
 */
#define ESP32C3_MESSAGE_SIZE    64
#define ESP32C3_MESSAGE_WORDS   (ESP32C3_MESSAGE_SIZE / sizeof(uint32_t))


/**
 * @brief Mode configuration for the SHA_MODE register.
 */
typedef enum {
    ESP32C3_SHA_1_MODE   = 0,
    ESP32C3_SHA_224_MODE = 1,
    ESP32C3_SHA_256_MODE = 2,
} ESP32C3ShaMode;


#define SHA_OP_TYPE_MASK    (1 << 0)
#define SHA_OP_DMA_MASK     (1 << 1)

typedef enum {
    OP_START         = 0,
    OP_CONTINUE      = 1,
    OP_DMA_START     = SHA_OP_DMA_MASK | OP_START,
    OP_DMA_CONTINUE  = SHA_OP_DMA_MASK | OP_CONTINUE,
} ESP32C3ShaOperation;

typedef union {
    struct sha256_state sha256;
    struct sha1_state   sha1;
} ESP32C3HashContext;


typedef void (*hash_init)(void *);
typedef void (*hash_compress)(void *, const uint8_t*);

typedef struct {
    hash_init init;
    /* For all types of hash, the message to "compress" must be 64-byte long (16 words of 32 bits) */
    hash_compress compress;
    /* Length of the context in bytes */
    size_t len;
} ESP32C3HashAlg;


typedef struct ESP32C3ShaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* SHA mode selected by the application */
    ESP32C3ShaMode mode;
    /* Context for the hash calculation */
    ESP32C3HashContext context;

    /* Resulted hash value */
    uint32_t hash[8];
    /* User data value */
    uint32_t message[ESP32C3_MESSAGE_WORDS];

    /* DMA related */
    /* Number of block to process in DMA mode */
    uint32_t block;
    bool int_ena;
    qemu_irq irq;

    /* Public: must be set before realizing instance*/
    ESP32C3GdmaState *gdma;
} ESP32C3ShaState;

typedef struct ESP32C3ShaClass {
    SysBusDeviceClass parent_class;
    /* Virtual methods*/
    void (*sha_start)(ESP32C3ShaState *s, ESP32C3ShaOperation op, uint32_t mode, uint32_t *message, uint32_t *hash);
} ESP32C3ShaClass;


REG32(SHA_MODE, 0x000)
    FIELD(SHA_MODE, MODE, 0, 3)

REG32(SHA_T_STRING, 0x004)

REG32(SHA_T_LENGTH, 0x008)

REG32(SHA_DMA_BLOCK_NUM, 0x00C)
    FIELD(SHA_DMA_BLOCK_NUM, DMA_BLOCK_NUM, 0, 6)

REG32(SHA_START, 0x010)
    FIELD(SHA_START, START, 0, 1)

REG32(SHA_CONTINUE, 0x014)
    FIELD(SHA_CONTINUE, CONTINUE, 0, 1)

REG32(SHA_BUSY, 0x018)
    FIELD(SHA_BUSY, BUSY_STATE, 0, 1)

REG32(SHA_DMA_START, 0x01C)
    FIELD(SHA_DMA_START, DMA_START, 0, 1)

REG32(SHA_DMA_CONTINUE, 0x020)
    FIELD(SHA_DMA_CONTINUE, DMA_CONTINUE, 0, 1)

REG32(SHA_CLEAR_IRQ, 0x024)
    FIELD(SHA_CLEAR_IRQ, CLEAR_INTERRUPT, 0, 1)

REG32(SHA_IRQ_ENA, 0x028)
    FIELD(SHA_IRQ_ENA, INTERRUPT_ENA, 0, 1)

REG32(SHA_DATE, 0x02C)
    FIELD(SHA_DATE, DATE, 0, 30)

REG32(SHA_H_MEM, 0x040)

REG32(SHA_M_MEM, 0x080)

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_aes.h"
#include "crypto/aes.h"

#define ESP32_AES_REGS_SIZE (A_AES_ENDIAN_REG + 4)

/*
 * process the value of the AES_MODE_REG
 */
static void esp32_aes_mode(Esp32AesState *s, uint32_t mode_value)
{
    if ((mode_value == 0) || (mode_value == 1) || (mode_value == 2)) {
        s->mode.type = ESP32_AES_ENCRYPTION_MODE;
    } else {
        s->mode.type = ESP32_AES_DECRYPTION_MODE;
    }
    if ((mode_value == 0) || (mode_value == 4)) {
        s->mode.bits = 128;
    } else if ((mode_value == 1) || (mode_value == 5)) {
        s->mode.bits = 192;
    } else {
        s->mode.bits = 256;
    }
}

/*
 * fill the full_key according to the mode bits
 * encrypt/decrypt according to mode value
 * set idle to 1
 */
static void esp32_aes_start(Esp32AesState *s)
{
    AES_KEY aes_key;
    uint32_t full_key[s->mode.bits / 32];
    memcpy(full_key, s->key, s->mode.bits / 8);
    if (s->mode.type == ESP32_AES_ENCRYPTION_MODE) {
        AES_set_encrypt_key((unsigned char *)full_key, s->mode.bits, &aes_key);
        AES_encrypt((unsigned char *)s->text, (unsigned char *)s->text, &aes_key);
    } else if (s->mode.type == ESP32_AES_DECRYPTION_MODE) {
        AES_set_decrypt_key((unsigned char *)full_key, s->mode.bits, &aes_key);
        AES_decrypt((unsigned char *)s->text, (unsigned char *)s->text, &aes_key);
    }
    s->aes_idle_reg = 1;
}

static uint64_t esp32_aes_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32AesState *s = ESP32_AES(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_AES_TEXT_0_REG ... A_AES_TEXT_3_REG:
        r = s->text[(addr - A_AES_TEXT_0_REG) / sizeof(uint32_t)];
        break;
    case A_AES_IDLE_REG:
        r = s->aes_idle_reg;
        break;
    }
    return r;
}


static void esp32_aes_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned int size)
{
    Esp32AesState *s = ESP32_AES(opaque);
    switch (addr) {
    case A_AES_TEXT_0_REG ... A_AES_TEXT_3_REG:
        s->text[(addr - A_AES_TEXT_0_REG) / sizeof(uint32_t)] = value;
        break;
    case A_AES_KEY_0_REG ... A_AES_KEY_7_REG:
        s->key[(addr - A_AES_KEY_0_REG) / sizeof(uint32_t)] = value;
        break;
    case A_AES_START_REG:
        s->aes_idle_reg = 0;
        esp32_aes_start(s);
        break;
    case A_AES_IDLE_REG:
        s->aes_idle_reg = value;
        break;
    case A_AES_MODE_REG:
        esp32_aes_mode(s, (uint32_t)value);
        break;
    }
}

static const MemoryRegionOps esp32_aes_ops = {
        .read =  esp32_aes_read,
        .write = esp32_aes_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_aes_reset(DeviceState *dev)
{
    Esp32AesState *s = ESP32_AES(dev);
    s->aes_idle_reg = 0;
}

static void esp32_aes_init(Object *obj)
{
    Esp32AesState *s = ESP32_AES(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_aes_ops, s,
                          TYPE_ESP32_AES, ESP32_AES_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32_aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_aes_reset;
}

static const TypeInfo esp32_aes_info = {
        .name = TYPE_ESP32_AES,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Esp32AesState),
        .instance_init = esp32_aes_init,
        .class_init = esp32_aes_class_init
};

static void esp32_aes_register_types(void)
{
    type_register_static(&esp32_aes_info);
}

type_init(esp32_aes_register_types)

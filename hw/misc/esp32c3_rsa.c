/*
 * ESP32-C3 RSA accelerator
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
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
#include "hw/boards.h"
#include "hw/misc/esp32c3_rsa.h"
#include "hw/irq.h"
#include <gcrypt.h>

#define ESP32C3_RSA_REGS_SIZE (A_RSA_DATE_REG + 4)

#define RSA_WARNING 0

static void copy_reversed(unsigned char* dest, size_t dst_size, const unsigned char* src, size_t src_size);
static bool mpi_block_to_gcrypt(const uint32_t *mem_block, size_t n_bytes, gcry_mpi_t *out);
static bool mpi_gcrypt_to_block(gcry_mpi_t in, uint32_t *mem_block);
static void esp32c3_rsa_modmul_start(ESP32C3RsaState *s);

/**
 * Convert between libgcrypt big-endian representation and little-endian hardware, or vice versa.
 * src_size should not exceed dst_size. The remaining part of dst is filled with 0.
 */
static void copy_reversed(unsigned char* dst, size_t dst_size, const unsigned char* src, size_t src_size)
{
    assert(src_size <= dst_size);
    size_t i;
    for (i = 0; i < src_size; ++i) {
        dst[i] = src[src_size - i - 1];
    }
    for (; i < dst_size; ++i) {
        dst[i] = 0;
    }
}


/**
 * Converts the little-endian memory block of the RSA peripheral to a new gcry_mpi_t object.
 * The caller is responsible for freeing the returned object.
 */
static bool mpi_block_to_gcrypt(const uint32_t *mem_block, size_t n_bytes, gcry_mpi_t *out)
{
    size_t scanned;
    const unsigned char* mem_u8 = (const unsigned char*) mem_block;
    unsigned char temp_buffer[ESP32C3_RSA_MEM_BLK_SIZE] = {};
    copy_reversed(temp_buffer, n_bytes, mem_u8, n_bytes);
    gcry_error_t err = gcry_mpi_scan(out, GCRYMPI_FMT_USG, temp_buffer, n_bytes, &scanned);
    if (err) {
        error_report("%s: gcry_mpi_scan failed with error: %s (%d)", __func__, gcry_strerror(err), err);
        return false;
    }
    if (scanned != n_bytes) {
        error_report("%s: gcry_mpi_scan scanned %zu, expected %zu", __func__, scanned, n_bytes);
        return false;
    }
    return true;
}


/**
 * Copies an MPI from gcry_mpi_t object to the RSA peripheral memory block.
 */
static bool mpi_gcrypt_to_block(gcry_mpi_t in, uint32_t *mem_block)
{
    size_t written;
    unsigned char* mem_u8 = (unsigned char*) mem_block;
    unsigned char temp_buffer[ESP32C3_RSA_MEM_BLK_SIZE] = {};
    gcry_error_t err = gcry_mpi_print(GCRYMPI_FMT_USG, temp_buffer, ESP32C3_RSA_MEM_BLK_SIZE, &written, in);
    if (err) {
        error_report("%s: gcry_mpi_print failed with error: %s (%d)", __func__, gcry_strerror(err), err);
        return false;
    }
    copy_reversed(mem_u8, ESP32C3_RSA_MEM_BLK_SIZE, temp_buffer, written);
    return true;
}


/** Calculates Z_MEM = X_MEM ^ Y_MEM mod M_MEM.
 *  Unlike the real hardware, doesn't use the mprime register.
 */
static void esp32c3_rsa_exp_mod(ESP32C3RsaState *s, uint32_t mode_reg, uint32_t *x_mem, uint32_t *y_mem, uint32_t *m_mem, uint32_t *z_mem, uint32_t int_ena)
{
    gcry_mpi_t x, y, z, m;

    /* Get the length of the operands in bytes. Register mode_reg designates the length
     * in 32-bit words. */
    size_t n_bytes = (mode_reg + 1) * 4;

    /* Convert inputs to gcry_mpi_t */
    if (!mpi_block_to_gcrypt(x_mem, n_bytes, &x)) {
        goto error_ret;
    }
    if (!mpi_block_to_gcrypt(y_mem, n_bytes, &y)) {
        goto error_x;
    }
    if (!mpi_block_to_gcrypt(m_mem, n_bytes, &m)) {
        goto error_y;
    }

    /* calculate the result and write it back */
    z = gcry_mpi_new(n_bytes * 8);
    gcry_mpi_powm(z, x, y, m);
    mpi_gcrypt_to_block(z, z_mem);

    /* Trigger an interrupt on completion */
    if (int_ena) {
        qemu_set_irq(s->irq, 1);
    }

    /* Clean up */
    gcry_mpi_release(m);
    gcry_mpi_release(z);
error_y:
    gcry_mpi_release(y);
error_x:
    gcry_mpi_release(x);
error_ret:
    return;
}


/* Calculates Z_MEM = X_MEM * Y_MEM mod M_MEM. */
static void esp32c3_rsa_modmul_start(ESP32C3RsaState *s)
{
    assert(s->mode_reg < (1 << 7));
    gcry_mpi_t m, x, z, y;

    /* In this mode, the output and input lengths are the same, mode_reg represents the length of
     * the operands in 32-bit word. Multiply by 4 to get the size in bytes. */
    const size_t n_bytes = (s->mode_reg + 1) * 4;

    /* Convert inputs to gcry_mpi_t */
    if (!mpi_block_to_gcrypt(s->x_mem, n_bytes, &x)) {
        goto error_ret;
    }
    if (!mpi_block_to_gcrypt(s->y_mem, n_bytes, &y)) {
        goto error_x;
    }
    if (!mpi_block_to_gcrypt(s->m_mem, n_bytes, &m)) {
        goto error_y;
    }

    z = gcry_mpi_new(n_bytes * 8 * 2);

    /* Z = X * Y mod M */
    gcry_mpi_mulm(z, x, y, m);

    /* Write back */
    mpi_gcrypt_to_block(z, s->z_mem);

    /* Trigger an interrupt on completion */
    if (s->int_ena) {
        qemu_set_irq(s->irq, 1);
    }

    /* Clean up */
    gcry_mpi_release(z);
    gcry_mpi_release(m);
error_y:
    gcry_mpi_release(y);
error_x:
    gcry_mpi_release(x);
error_ret:
    return;
}


/** Calculates Z_MEM = X_MEM * Z_MEM */
static void esp32c3_rsa_mul_start(ESP32C3RsaState *s)
{
    /* In this mode, the output length, in 32-bit word, is set by mode_reg. The input is length / 2.
     * Thus, multiply mode_reg by 4 to get the number of bytes. */
    size_t n_bytes = (s->mode_reg + 1) * 4;
    size_t n_bytes_input = n_bytes / 2;

    memcpy(s->z_mem, s->z_mem + n_bytes_input / sizeof(uint32_t), n_bytes_input);
    memset(s->z_mem + n_bytes_input / sizeof(uint32_t), 0, n_bytes_input);

    /* Convert inputs to gcry_mpi_t */
    gcry_mpi_t x, z, result;
    if (!mpi_block_to_gcrypt(s->x_mem, n_bytes, &x)) {
        goto error_ret;
    }
    if (!mpi_block_to_gcrypt(s->z_mem, n_bytes, &z)) {
        goto error_x;
    }

    /* Multiply */
    result = gcry_mpi_new(n_bytes * 8);
    gcry_mpi_mul(result, x, z);
    mpi_gcrypt_to_block(result, s->z_mem);

    /* Trigger an interrupt on completion */
    if (s->int_ena) {
        qemu_set_irq(s->irq, 1);
    }

    /* Clean up */
    gcry_mpi_release(result);
    gcry_mpi_release(z);
error_x:
    gcry_mpi_release(x);
error_ret:
    return;
}


static void esp32c3_rsa_clean_mem(ESP32C3RsaState *s)
{
    memset(s->m_mem, 0, sizeof(s->m_mem));
    memset(s->x_mem, 0, sizeof(s->x_mem));
    memset(s->y_mem, 0, sizeof(s->y_mem));
    memset(s->z_mem, 0, sizeof(s->z_mem));
}

static uint64_t esp32c3_rsa_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3RsaState *s = ESP32C3_RSA(opaque);
    uint64_t r = 0;

    switch (addr) {
        case A_RSA_MEM_M_BLOCK_BASE ... (A_RSA_MEM_M_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            r = s->m_mem[(addr - A_RSA_MEM_M_BLOCK_BASE) / sizeof(uint32_t)];
            break;

        case A_RSA_MEM_Z_BLOCK_BASE ... (A_RSA_MEM_Z_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            r = s->z_mem[(addr - A_RSA_MEM_Z_BLOCK_BASE) / sizeof(uint32_t)];
            break;

        case A_RSA_MEM_Y_BLOCK_BASE ... (A_RSA_MEM_Y_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            r = s->y_mem[(addr - A_RSA_MEM_Y_BLOCK_BASE) / sizeof(uint32_t)];
            break;

        case A_RSA_MEM_X_BLOCK_BASE ... (A_RSA_MEM_X_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            r = s->x_mem[(addr - A_RSA_MEM_X_BLOCK_BASE) / sizeof(uint32_t)];
            break;

        case A_RSA_M_PRIME_REG:
            r = s->mprime_reg;
            break;

        case A_RSA_MODE_REG:
            r = s->mode_reg;
            break;

        case A_RSA_CONSTANT_TIME_REG:
            r = s->const_time_reg;
            break;

        case A_RSA_SEARCH_ENABLE_REG:
            r = s->search_ena_reg;
            break;

        case A_RSA_SEARCH_POS_REG:
            r = s->search_pos_reg;
            break;

        case A_RSA_CLEAN_REG:
            esp32c3_rsa_clean_mem(s);
            r = 1;
            break;

        case A_RSA_IDLE_REG:
            /* Always Idle */
            r = 1;
            break;

        case A_RSA_INTERRUPT_ENA_REG:
            r = s->int_ena;
            break;

        default:
#if RSA_WARNING
            warn_report("[RSA] Unsupported read to register %08x\n", addr);
#endif
            break;

    }

    return r;
}


static void esp32c3_rsa_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32C3RsaClass *class = ESP32C3_RSA_GET_CLASS(opaque);
    ESP32C3RsaState *s = ESP32C3_RSA(opaque);

    switch (addr) {

        case A_RSA_MEM_M_BLOCK_BASE ... (A_RSA_MEM_M_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            s->m_mem[(addr - A_RSA_MEM_M_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            break;

        case A_RSA_MEM_Z_BLOCK_BASE ... (A_RSA_MEM_Z_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            s->z_mem[(addr - A_RSA_MEM_Z_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            break;

        case A_RSA_MEM_Y_BLOCK_BASE ... (A_RSA_MEM_Y_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            s->y_mem[(addr - A_RSA_MEM_Y_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            break;

        case A_RSA_MEM_X_BLOCK_BASE ... (A_RSA_MEM_X_BLOCK_BASE + ESP32C3_RSA_MEM_BLK_SIZE - 1):
            s->x_mem[(addr - A_RSA_MEM_X_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            break;

        case A_RSA_M_PRIME_REG:
            s->mprime_reg = value;
            break;

        case A_RSA_MODE_REG:
            s->mode_reg = FIELD_EX32(value, RSA_MODE_REG, RSA_MODE);
            break;

        case A_RSA_CONSTANT_TIME_REG:
            s->const_time_reg = FIELD_EX32(value, RSA_CONSTANT_TIME_REG, RSA_CONSTANT_TIME);
            break;

        case A_RSA_SEARCH_ENABLE_REG:
            s->search_ena_reg = FIELD_EX32(value, RSA_SEARCH_ENABLE_REG, RSA_SEARCH_ENABLE);
            break;

        case A_RSA_SEARCH_POS_REG:
            s->search_pos_reg = FIELD_EX32(value, RSA_SEARCH_POS_REG, RSA_SEARCH_POS);
            break;

        case A_RSA_MODEXP_START_REG:
            if (FIELD_EX32(value, RSA_MODEXP_START_REG, RSA_MODEXP_START)) {
                class->rsa_exp_mod(s, s->mode_reg, s->x_mem, s->y_mem, s->m_mem, s->z_mem, s->int_ena);
            }
            break;

        case A_RSA_MODMULT_START_REG:
            if (FIELD_EX32(value, RSA_MODMULT_START_REG, RSA_MODMULT_START)) {
                esp32c3_rsa_modmul_start(s);
            }
            break;

        case A_RSA_MULT_START_REG:
            if (FIELD_EX32(value, RSA_MULT_START_REG, RSA_MULT_START)) {
                esp32c3_rsa_mul_start(s);
            }
            break;

        case A_RSA_CLEAR_INTERRUPT_REG:
            if (FIELD_EX32(value, RSA_CLEAR_INTERRUPT_REG, RSA_CLEAR_INTERRUPT)) {
                qemu_irq_lower(s->irq);
            }
            break;

        case A_RSA_INTERRUPT_ENA_REG:
            s->int_ena = FIELD_EX32(value, RSA_INTERRUPT_ENA_REG, RSA_INTERRUPT_ENA);
            break;

        default:
#if RSA_WARNING
            warn_report("[RSA] Unsupported write to register %08x\n", addr);
#endif
            break;
    }

}

static const MemoryRegionOps esp32c3_rsa_ops = {
    .read =  esp32c3_rsa_read,
    .write = esp32c3_rsa_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_rsa_reset(DeviceState *dev)
{
    ESP32C3RsaState *s = ESP32C3_RSA(dev);

    esp32c3_rsa_clean_mem(s);

    /* Clear any spurious interrupt */
    s->int_ena = 0;
    qemu_irq_lower(s->irq);
}

static void esp32c3_rsa_init(Object *obj)
{
    ESP32C3RsaState *s = ESP32C3_RSA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_rsa_ops, s,
                          TYPE_ESP32C3_RSA, ESP32C3_RSA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq);
}

static void esp32c3_rsa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32C3RsaClass* esp32c3_rsa = ESP32C3_RSA_CLASS(klass);

    dc->reset = esp32c3_rsa_reset;

    esp32c3_rsa->rsa_exp_mod = esp32c3_rsa_exp_mod;
}

static const TypeInfo esp32c3_rsa_info = {
    .name = TYPE_ESP32C3_RSA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3RsaState),
    .instance_init = esp32c3_rsa_init,
    .class_init = esp32c3_rsa_class_init,
    .class_size = sizeof(ESP32C3RsaClass)
};

static void esp32c3_rsa_register_types(void)
{
    type_register_static(&esp32c3_rsa_info);
}

type_init(esp32c3_rsa_register_types)

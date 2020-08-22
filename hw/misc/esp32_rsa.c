/*
 * ESP32 RSA accelerator
 *
 * Copyright (c) 2020 Espressif Systems (Shanghai) Co. Ltd.
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
#include "hw/misc/esp32_rsa.h"
#include <gcrypt.h>


#define ESP32_RSA_REGS_SIZE (A_RSA_QUERY_CLEAN_REG + 4)

static void copy_reversed(unsigned char* dest, size_t dst_size, const unsigned char* src, size_t src_size);
static bool mpi_block_to_gcrypt(const uint32_t* mem_block, size_t n_bytes, gcry_mpi_t *out);
static bool mpi_gcrypt_to_block(gcry_mpi_t in, uint32_t* mem_block);
static void esp32_rsa_exp_mod(Esp32RsaState *s);
static void esp32_rsa_mul_start(Esp32RsaState *s);
static void esp32_rsa_mul_op(Esp32RsaState *s);
static void esp32_rsa_mod_mul_op(Esp32RsaState *s);


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
static bool mpi_block_to_gcrypt(const uint32_t* mem_block, size_t n_bytes, gcry_mpi_t *out)
{
    size_t scanned;
    const unsigned char* mem_u8 = (const unsigned char*) mem_block;
    unsigned char temp_buffer[ESP32_RSA_MEM_BLK_SIZE] = {};
    copy_reversed(temp_buffer, n_bytes, mem_u8, n_bytes);
    gcry_error_t err = gcry_mpi_scan(out, GCRYMPI_FMT_USG, temp_buffer, n_bytes, &scanned);
    if (err) {
        fprintf(stderr, "%s: gcry_mpi_scan failed with error: %s (%d)", __func__, gcry_strerror(err), err);
        return false;
    }
    if (scanned != n_bytes) {
        fprintf(stderr, "%s: gcry_mpi_scan scanned %zu, expected %zu", __func__, scanned, n_bytes);
        return false;
    }
    return true;
}

/**
 * Copies an MPI from gcry_mpi_t object to the RSA peripheral memory block.
 */
static bool mpi_gcrypt_to_block(gcry_mpi_t in, uint32_t* mem_block)
{
    size_t written;
    unsigned char* mem_u8 = (unsigned char*) mem_block;
    unsigned char temp_buffer[ESP32_RSA_MEM_BLK_SIZE] = {};
    gcry_error_t err = gcry_mpi_print(GCRYMPI_FMT_USG, temp_buffer, ESP32_RSA_MEM_BLK_SIZE, &written, in);
    if (err) {
        fprintf(stderr, "%s: gcry_mpi_print failed with error: %s (%d)", __func__, gcry_strerror(err), err);
        return false;
    }
    copy_reversed(mem_u8, ESP32_RSA_MEM_BLK_SIZE, temp_buffer, written);
    return true;
}

/** Calculates Z_MEM = X_MEM ^ Y_MEM mod M_MEM.
 *  Unlike the real hardware, doesn't use the mprime register.
 */
static void esp32_rsa_exp_mod(Esp32RsaState *s)
{
    gcry_mpi_t x, y, z, m;

    size_t n_bytes = (s->rsa_modexp_mode_reg + 1) * 64;

    /* convert inputs to gcry_mpi_t */
    if (!mpi_block_to_gcrypt(s->rsa_x_mem, n_bytes, &x) ||
        !mpi_block_to_gcrypt(s->rsa_y_mem, n_bytes, &y) ||
        !mpi_block_to_gcrypt(s->rsa_m_mem, n_bytes, &m)) {
        return;
    }

    /* calculate the result and write it back */
    z = gcry_mpi_new(n_bytes);
    gcry_mpi_powm(z, x, y, m);
    mpi_gcrypt_to_block(z, s->rsa_z_mem);

    /* clean up */
    gcry_mpi_release(x);
    gcry_mpi_release(y);
    gcry_mpi_release(z);
    gcry_mpi_release(m);

    /* indicate that the operation is complete */
    s->rsa_q_int_reg = 1;
}


static void esp32_rsa_mul_start(Esp32RsaState *s)
{
    /* Hardware does different operations depending on rsa_mult_mode_reg value: */
    bool is_mod_mult = (s->rsa_mult_mode_reg < 8);
    if (is_mod_mult) {
        esp32_rsa_mod_mul_op(s);
    } else {
        esp32_rsa_mul_op(s);
    }
}

/** Calculates Z_MEM = X_MEM * (Z_MEM >> n) */
static void esp32_rsa_mul_op(Esp32RsaState *s)
{
    assert(s->rsa_mult_mode_reg >= 8 && s->rsa_mult_mode_reg < 16);
    /* In this mode, the output length is set by rsa_mult_mode_reg,
     * and the length of inputs is half of that.
     * Z input is shifted (multiplied by 2^(input length in bits)),
     * and needs to be shifted back before passing to gcry_mpi_mul.
     */
    size_t n_bytes = (s->rsa_mult_mode_reg - 8 + 1) * 64;
    size_t n_bytes_input = n_bytes / 2;
    memcpy(s->rsa_z_mem, s->rsa_z_mem + n_bytes_input / sizeof(s->rsa_z_mem[0]), n_bytes_input);
    memset(s->rsa_z_mem + n_bytes_input / sizeof(s->rsa_z_mem[0]), 0, n_bytes_input);
    /* convert inputs to gcry_mpi_t */
    gcry_mpi_t x, z, result;
    if (!mpi_block_to_gcrypt(s->rsa_x_mem, n_bytes, &x) ||
        !mpi_block_to_gcrypt(s->rsa_z_mem, n_bytes, &z)) {
        return;
    }
    /* multiply */
    result = gcry_mpi_new(n_bytes * 8);
    gcry_mpi_mul(result, x, z);
    mpi_gcrypt_to_block(result, s->rsa_z_mem);

    /* clean up */
    gcry_mpi_release(x);
    gcry_mpi_release(z);
    gcry_mpi_release(result);

    /* indicate that the operation is complete */
    s->rsa_q_int_reg = 1;
}

/** Calculates Z_MEM = Z_MEM * X_MEM * R^-1 mod M_MEM.
 *
 *  Real hardware does this using Montgomery multiplication
 *  algorithm. Here we simply call the modular multiplication function
 *  twice.
 *  R^-1 is re-calculated if M_MEM is modified.
 *  M' (mprime) register value is ignored in this simulation.
 */
static void esp32_rsa_mod_mul_op(Esp32RsaState *s)
{
    assert(s->rsa_mult_mode_reg < 8);
    /* In this mode, the output and input lengths are the same */
    size_t n_bytes = (s->rsa_mult_mode_reg + 1) * 64;
    gcry_mpi_t m;
    if (!mpi_block_to_gcrypt(s->rsa_m_mem, n_bytes, &m)) {
        return;
    }
    /* Calculate R^-1 if it hasn't been calculated yet */
    if (!s->cache.valid) {
        if (!s->cache.rinv) {
            s->cache.rinv = gcry_mpi_new(n_bytes * 8);
        }
        gcry_mpi_t r = gcry_mpi_new(n_bytes * 8 + 1);
        gcry_mpi_set_bit(r, n_bytes * 8);
        if (!gcry_mpi_invm(s->cache.rinv, r, m)) {
            qemu_log("%s: failed to calculate modulo inverse\n", __func__);
            return;
        }
        s->cache.valid = true;
    }

    /* convert inputs to gcry_mpi_t */
    gcry_mpi_t x, z, res1, res2;
    if (!mpi_block_to_gcrypt(s->rsa_x_mem, n_bytes, &x) ||
        !mpi_block_to_gcrypt(s->rsa_z_mem, n_bytes, &z)) {
        gcry_mpi_release(m);
        return;
    }

    /* temporaries */
    res1 = gcry_mpi_new(n_bytes * 8 * 2);
    res2 = gcry_mpi_new(n_bytes * 8 * 2);

    /* res1 = X * Z mod M */
    gcry_mpi_mulm(res1, x, z, m);
    /* res2 = X * Z * Rinv mod M */
    gcry_mpi_mulm(res2, res1, s->cache.rinv, m);

    /* write back */
    mpi_gcrypt_to_block(res2, s->rsa_z_mem);

    /* clean up */
    gcry_mpi_release(x);
    gcry_mpi_release(z);
    gcry_mpi_release(res1);

    /* indicate that the operation is complete */
    s->rsa_q_int_reg = 1;
}


static void esp32_rsa_clean_mem(Esp32RsaState *s)
{
    memset(s->rsa_m_mem, 0, sizeof(s->rsa_m_mem));
    memset(s->rsa_x_mem, 0, sizeof(s->rsa_x_mem));
    memset(s->rsa_y_mem, 0, sizeof(s->rsa_y_mem));
    memset(s->rsa_z_mem, 0, sizeof(s->rsa_z_mem));
    s->cache.valid = false;
}

static uint64_t esp32_rsa_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32RsaState *s = ESP32_RSA(opaque);
    uint64_t val = 0;

    switch (addr) {
        case A_RSA_MEM_Z_BLOCK_BASE ... (A_RSA_MEM_Z_BLOCK_BASE + ESP32_RSA_MEM_BLK_SIZE - 1):
            val = s->rsa_z_mem[(addr - A_RSA_MEM_Z_BLOCK_BASE) / sizeof(uint32_t)];
            break;

        case A_RSA_QUERY_CLEAN_REG:
            /* After coming out from reset, RSA Accelerator first initialize
             * internal memory block to zeros before turning this register to 1.
             * Software poll this register to read 1, before using the internal
             * memory blocks. Internal memory block initialisation performed
             * here before returning this read operation to 1.
             */
            esp32_rsa_clean_mem(s);
            val = s->rsa_clean_reg;
            break;

        case A_RSA_QUERY_INTERRUPT_REG:
            val = s->rsa_q_int_reg;
            break;
    }

    return val;
}


static void esp32_rsa_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32RsaState *s = ESP32_RSA(opaque);

    switch (addr) {

        case A_RSA_MEM_M_BLOCK_BASE ... (A_RSA_MEM_M_BLOCK_BASE + ESP32_RSA_MEM_BLK_SIZE - 1):
            s->rsa_m_mem[(addr - A_RSA_MEM_M_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            s->cache.valid = false;
            break;

        case A_RSA_MEM_RB_BLOCK_BASE ... (A_RSA_MEM_RB_BLOCK_BASE + ESP32_RSA_MEM_BLK_SIZE - 1):
            s->rsa_z_mem[(addr - A_RSA_MEM_RB_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            break;

        case A_RSA_MEM_Y_BLOCK_BASE ... (A_RSA_MEM_Y_BLOCK_BASE + ESP32_RSA_MEM_BLK_SIZE - 1):
            s->rsa_y_mem[(addr - A_RSA_MEM_Y_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            break;

        case A_RSA_MEM_X_BLOCK_BASE ... (A_RSA_MEM_X_BLOCK_BASE + ESP32_RSA_MEM_BLK_SIZE - 1):
            s->rsa_x_mem[(addr - A_RSA_MEM_X_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t)value;
            break;

        case A_RSA_M_DASH_REG:
            s->rsa_mprime_reg = value;
            break;

        case A_RSA_MODEXP_MODE_REG:
            s->rsa_modexp_mode_reg = value;
            break;

        case A_RSA_MULT_MODE_REG:
            s->rsa_mult_mode_reg = value;
            break;

        case A_RSA_MODEXP_START_REG:
            esp32_rsa_exp_mod(s);
            break;

        case A_RSA_MULT_START_REG:
            esp32_rsa_mul_start(s);
            break;

        case A_RSA_QUERY_INTERRUPT_REG:
            /* Clear on write register */
            s->rsa_q_int_reg &= ~value;
            break;
    }

}

static const MemoryRegionOps esp32_rsa_ops = {
    .read =  esp32_rsa_read,
    .write = esp32_rsa_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_rsa_reset(DeviceState *dev)
{
    Esp32RsaState *s = ESP32_RSA(dev);

    esp32_rsa_clean_mem(s);

    /* Clear any spurious interrupt */
    s->rsa_q_int_reg = 0;

    /* RSA memory block initialization complete */
    s->rsa_clean_reg = 1;
}

static void esp32_rsa_init(Object *obj)
{
    Esp32RsaState *s = ESP32_RSA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_rsa_ops, s,
                          TYPE_ESP32_RSA, ESP32_RSA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32_rsa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_rsa_reset;
}

static const TypeInfo esp32_rsa_info = {
    .name = TYPE_ESP32_RSA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32RsaState),
    .instance_init = esp32_rsa_init,
    .class_init = esp32_rsa_class_init
};

static void esp32_rsa_register_types(void)
{
    type_register_static(&esp32_rsa_info);
}

type_init(esp32_rsa_register_types)

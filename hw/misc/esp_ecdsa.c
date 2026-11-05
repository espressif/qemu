/*
 * ECDSA accelerator of ESP32-H2 and newer chips
 *
 * Models verification with a digest written by software, which is what mbedtls
 * reaches. Signing and public key export need a key in eFuse or the key manager
 * and are not modelled: they run through the states and return zeros.
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/misc/esp_ecdsa.h"
#include "hw/misc/esp_ecc_mpi.h"

#define ECDSA_MODE_VERIFY   0

#define ECDSA_STATE_IDLE    0
#define ECDSA_STATE_LOAD    1
#define ECDSA_STATE_GET     2

#define ECDSA_INT_PREP_DONE BIT(0)
#define ECDSA_INT_PROC_DONE BIT(1)
#define ECDSA_INT_POST_DONE BIT(2)

enum { PARAM_R, PARAM_S, PARAM_Z, PARAM_QAX, PARAM_QAY };

static void esp_ecdsa_update_irq(ESPEcdsaState *s)
{
    qemu_set_irq(s->irq, (s->int_raw & s->int_ena) != 0);
}

static void esp_ecdsa_set_int(ESPEcdsaState *s, uint32_t bits)
{
    s->int_raw |= bits;
    esp_ecdsa_update_irq(s);
}

/* Checks that x(z/s * G + r/s * Q) mod n == r */
static bool esp_ecdsa_check(ESPEcdsaState *s, gcry_ctx_t ctx, size_t len)
{
    gcry_mpi_t n = gcry_mpi_ec_get_mpi("n", ctx, 1);
    gcry_mpi_point_t g = gcry_mpi_ec_get_point("g", ctx, 1);
    gcry_mpi_t r = esp_ecc_mpi_from_le(s->mem[PARAM_R], len);
    gcry_mpi_t sig_s = esp_ecc_mpi_from_le(s->mem[PARAM_S], len);
    gcry_mpi_t z = esp_ecc_mpi_from_le(s->mem[PARAM_Z], len);
    gcry_mpi_point_t q = esp_ecc_point_from_le(s->mem[PARAM_QAX], s->mem[PARAM_QAY], len);
    gcry_mpi_t w = gcry_mpi_new(0);
    gcry_mpi_t u1 = gcry_mpi_new(0);
    gcry_mpi_t u2 = gcry_mpi_new(0);
    gcry_mpi_t x = gcry_mpi_new(0);
    gcry_mpi_point_t p1 = gcry_mpi_point_new(0);
    gcry_mpi_point_t p2 = gcry_mpi_point_new(0);
    bool valid = false;

    if (gcry_mpi_cmp_ui(r, 0) > 0 && gcry_mpi_cmp(r, n) < 0 &&
        gcry_mpi_cmp_ui(sig_s, 0) > 0 && gcry_mpi_cmp(sig_s, n) < 0 &&
        gcry_mpi_ec_curve_point(q, ctx)) {
        gcry_mpi_invm(w, sig_s, n);
        gcry_mpi_mulm(u1, z, w, n);
        gcry_mpi_mulm(u2, r, w, n);
        gcry_mpi_ec_mul(p1, u1, g, ctx);
        gcry_mpi_ec_mul(p2, u2, q, ctx);
        gcry_mpi_ec_add(p1, p1, p2, ctx);
        if (gcry_mpi_ec_get_affine(x, NULL, p1, ctx) == 0) {
            gcry_mpi_mod(x, x, n);
            valid = gcry_mpi_cmp(x, r) == 0;
        }
    }

    gcry_mpi_point_release(p2);
    gcry_mpi_point_release(p1);
    gcry_mpi_release(x);
    gcry_mpi_release(u2);
    gcry_mpi_release(u1);
    gcry_mpi_release(w);
    gcry_mpi_point_release(q);
    gcry_mpi_release(z);
    gcry_mpi_release(sig_s);
    gcry_mpi_release(r);
    gcry_mpi_point_release(g);
    gcry_mpi_release(n);
    return valid;
}

static void esp_ecdsa_verify(ESPEcdsaState *s)
{
    const ESPEcdsaLayout *layout = ESP_ECDSA_GET_CLASS(s)->layout;
    const uint32_t curve = extract32(s->conf, layout->curve_shift, layout->curve_bits);
    gcry_ctx_t ctx;
    size_t len = 0;
    const char *name = esp_ecc_curve_name(curve, &len);

    s->result = 0;
    if (name == NULL) {
        qemu_log_mask(LOG_UNIMP, "esp_ecdsa: curve %u is not modelled\n", curve);
    } else if (!extract32(s->conf, layout->set_z_shift, 1)) {
        qemu_log_mask(LOG_UNIMP, "esp_ecdsa: a digest from the SHA block is not modelled\n");
    } else if (gcry_mpi_ec_new(&ctx, NULL, name) == 0) {
        s->result = esp_ecdsa_check(s, ctx, len);
        gcry_ctx_release(ctx);
    }
}

static void esp_ecdsa_start(ESPEcdsaState *s, uint32_t value)
{
    const ESPEcdsaLayout *layout = ESP_ECDSA_GET_CLASS(s)->layout;
    const uint32_t mode = extract32(s->conf, 0, layout->mode_bits);

    if ((value & R_ECDSA_START_START_MASK) && s->state == ECDSA_STATE_IDLE) {
        s->state = ECDSA_STATE_LOAD;
        esp_ecdsa_set_int(s, ECDSA_INT_PREP_DONE);
    } else if ((value & R_ECDSA_START_LOAD_DONE_MASK) && s->state == ECDSA_STATE_LOAD) {
        if (mode == ECDSA_MODE_VERIFY) {
            esp_ecdsa_verify(s);
            s->state = ECDSA_STATE_IDLE;
            esp_ecdsa_set_int(s, ECDSA_INT_PROC_DONE);
        } else {
            qemu_log_mask(LOG_UNIMP, "esp_ecdsa: work mode %u is not modelled\n", mode);
            memset(s->mem, 0, sizeof(s->mem));
            s->state = ECDSA_STATE_GET;
            esp_ecdsa_set_int(s, ECDSA_INT_PROC_DONE);
        }
    } else if ((value & R_ECDSA_START_GET_DONE_MASK) && s->state == ECDSA_STATE_GET) {
        s->state = ECDSA_STATE_IDLE;
        esp_ecdsa_set_int(s, ECDSA_INT_POST_DONE);
    }
}

/* Returns the parameter register at addr, or NULL if addr is not in the memory */
static uint32_t *esp_ecdsa_mem(ESPEcdsaState *s, hwaddr addr)
{
    const ESPEcdsaLayout *layout = ESP_ECDSA_GET_CLASS(s)->layout;

    if (addr < layout->mem_base ||
        addr >= layout->mem_base + ESP_ECDSA_PARAM_COUNT * layout->mem_stride) {
        return NULL;
    }
    addr -= layout->mem_base;
    return &s->mem[addr / layout->mem_stride][(addr % layout->mem_stride) / 4];
}

static uint64_t esp_ecdsa_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPEcdsaState *s = ESP_ECDSA(opaque);
    uint32_t *word;

    switch (addr) {
    case A_ECDSA_CONF:
        return s->conf;
    case A_ECDSA_INT_RAW:
        return s->int_raw;
    case A_ECDSA_INT_ST:
        return s->int_raw & s->int_ena;
    case A_ECDSA_INT_ENA:
        return s->int_ena;
    case A_ECDSA_STATE:
        return s->state;
    case A_ECDSA_RESULT:
        return s->result;
    case A_ECDSA_DATE:
        return ESP_ECDSA_GET_CLASS(s)->layout->date;
    default:
        word = esp_ecdsa_mem(s, addr);
        return word ? *word : 0;
    }
}

static void esp_ecdsa_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESPEcdsaState *s = ESP_ECDSA(opaque);
    uint32_t *word;

    switch (addr) {
    case A_ECDSA_CONF:
        s->conf = value;
        break;
    case A_ECDSA_INT_ENA:
        s->int_ena = value;
        esp_ecdsa_update_irq(s);
        break;
    case A_ECDSA_INT_CLR:
        s->int_raw &= ~value;
        esp_ecdsa_update_irq(s);
        break;
    case A_ECDSA_START:
        esp_ecdsa_start(s, value);
        break;
    default:
        word = esp_ecdsa_mem(s, addr);
        if (word) {
            *word = value;
        }
        break;
    }
}

static const MemoryRegionOps esp_ecdsa_ops = {
    .read = esp_ecdsa_read,
    .write = esp_ecdsa_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void esp_ecdsa_reset_hold(Object *obj, ResetType type)
{
    ESPEcdsaState *s = ESP_ECDSA(obj);

    s->conf = 0;
    s->state = ECDSA_STATE_IDLE;
    s->result = 0;
    s->int_raw = 0;
    s->int_ena = 0;
    memset(s->mem, 0, sizeof(s->mem));
    qemu_irq_lower(s->irq);
}

static void esp_ecdsa_init(Object *obj)
{
    ESPEcdsaState *s = ESP_ECDSA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_ecdsa_ops, s, TYPE_ESP_ECDSA, ESP_ECDSA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void esp_ecdsa_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp_ecdsa_reset_hold;
}

static void esp_ecdsa_chip_class_init(ObjectClass *klass, void *data)
{
    ESP_ECDSA_CLASS(klass)->layout = data;
}

/*
 * The field positions are from soc/<chip>/register/soc/ecdsa_reg.h in IDF.
 * ESP32-H2 has this layout on chips before v1.2, and QEMU's default eFuse
 * reports v0.0. IDF chooses the layout from DATE and the chip revision.
 * DATE is the value read from a v0.0 board.
 */
static const ESPEcdsaLayout esp32h2_ecdsa_layout = {
    .mode_bits = 1,
    .curve_shift = 1,
    .curve_bits = 1,
    .set_z_shift = 3,
    .mem_base = 0xA00,
    .mem_stride = 0x20,
    .date = 0x2207180,
};

static const ESPEcdsaLayout esp32c61_ecdsa_layout = {
    .mode_bits = 2,
    .curve_shift = 2,
    .curve_bits = 1,
    .set_z_shift = 4,
    .mem_base = 0x340,
    .mem_stride = 0x20,
    .date = 0x2306130,
};

/* ESP32-C5, ESP32-H4 and ESP32-P4 v3 */
static const ESPEcdsaLayout esp_ecdsa_p384_layout = {
    .mode_bits = 2,
    .curve_shift = 2,
    .curve_bits = 2,
    .set_z_shift = 5,
    .mem_base = 0x3E0,
    .mem_stride = 0x30,
    .date = 0x2409180,
};

#define ESP_ECDSA_CHIP(chip_type, chip_layout) {    \
    .name = chip_type,                              \
    .parent = TYPE_ESP_ECDSA,                       \
    .class_init = esp_ecdsa_chip_class_init,        \
    .class_data = (void *)&chip_layout,             \
}

static const TypeInfo esp_ecdsa_types[] = {
    {
        .name = TYPE_ESP_ECDSA,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(ESPEcdsaState),
        .instance_init = esp_ecdsa_init,
        .class_init = esp_ecdsa_class_init,
        .class_size = sizeof(ESPEcdsaClass),
        .abstract = true,
    },
    ESP_ECDSA_CHIP(TYPE_ESP32C5_ECDSA, esp_ecdsa_p384_layout),
    ESP_ECDSA_CHIP(TYPE_ESP32C61_ECDSA, esp32c61_ecdsa_layout),
    ESP_ECDSA_CHIP(TYPE_ESP32H2_ECDSA, esp32h2_ecdsa_layout),
    ESP_ECDSA_CHIP(TYPE_ESP32H4_ECDSA, esp_ecdsa_p384_layout),
    ESP_ECDSA_CHIP(TYPE_ESP32P4_ECDSA, esp_ecdsa_p384_layout),
};

DEFINE_TYPES(esp_ecdsa_types)

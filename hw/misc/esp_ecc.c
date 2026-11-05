/*
 * ECC accelerator of ESP32-C6 and newer chips
 *
 * Models the modes that mbedtls reaches: point multiplication and point
 * verification. Operations finish at once, so INT_RAW is set when START is
 * written. The extended modes (Jacobian, point addition, modular arithmetic)
 * are not modelled.
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
#include "hw/misc/esp_ecc.h"
#include "hw/misc/esp_ecc_mpi.h"

#define ECC_MODE_POINT_MUL          0
#define ECC_MODE_VERIFY             2
#define ECC_MODE_VERIFY_THEN_MUL    3

enum { PARAM_K, PARAM_PX, PARAM_PY };

const char *esp_ecc_curve_name(uint32_t curve, size_t *len)
{
    switch (curve) {
    case 0:
        *len = 24;
        return "NIST P-192";
    case 1:
        *len = 32;
        return "NIST P-256";
    case 2:
        *len = 48;
        return "NIST P-384";
    default:
        return NULL;
    }
}

gcry_mpi_t esp_ecc_mpi_from_le(const uint32_t *mem, size_t len)
{
    uint8_t be[ESP_ECC_MAX_BYTES];
    gcry_mpi_t value;

    for (size_t i = 0; i < len; i++) {
        be[len - i - 1] = mem[i / 4] >> (8 * (i % 4));
    }
    gcry_mpi_scan(&value, GCRYMPI_FMT_USG, be, len, NULL);
    return value;
}

void esp_ecc_mpi_to_le(gcry_mpi_t value, uint32_t *mem, size_t len)
{
    uint8_t be[ESP_ECC_MAX_BYTES];
    size_t written = 0;

    gcry_mpi_print(GCRYMPI_FMT_USG, be, sizeof(be), &written, value);
    memset(mem, 0, len);
    for (size_t i = 0; i < written && i < len; i++) {
        mem[i / 4] |= (uint32_t)be[written - i - 1] << (8 * (i % 4));
    }
}

gcry_mpi_point_t esp_ecc_point_from_le(const uint32_t *x, const uint32_t *y, size_t len)
{
    return gcry_mpi_point_snatch_set(NULL, esp_ecc_mpi_from_le(x, len),
                                     esp_ecc_mpi_from_le(y, len),
                                     gcry_mpi_set_ui(NULL, 1));
}

static void esp_ecc_update_irq(ESPEccState *s)
{
    qemu_set_irq(s->irq, s->int_raw && s->int_ena);
}

static void esp_ecc_point_mul(ESPEccState *s, gcry_ctx_t ctx, gcry_mpi_point_t point, size_t len)
{
    gcry_mpi_t k = esp_ecc_mpi_from_le(s->mem[PARAM_K], len);
    gcry_mpi_point_t result = gcry_mpi_point_new(0);
    gcry_mpi_t x = gcry_mpi_new(0);
    gcry_mpi_t y = gcry_mpi_new(0);

    gcry_mpi_ec_mul(result, k, point, ctx);
    /* The point at infinity has no affine form */
    if (gcry_mpi_ec_get_affine(x, y, result, ctx) == 0) {
        esp_ecc_mpi_to_le(x, s->mem[PARAM_PX], len);
        esp_ecc_mpi_to_le(y, s->mem[PARAM_PY], len);
    } else {
        memset(s->mem[PARAM_PX], 0, len);
        memset(s->mem[PARAM_PY], 0, len);
    }

    gcry_mpi_release(y);
    gcry_mpi_release(x);
    gcry_mpi_point_release(result);
    gcry_mpi_release(k);
}

static void esp_ecc_start(ESPEccState *s)
{
    const ESPEccLayout *layout = ESP_ECC_GET_CLASS(s)->layout;
    const uint32_t mode = extract32(s->conf, layout->work_mode_shift, layout->work_mode_bits);
    const uint32_t curve = extract32(s->conf, R_ECC_CONF_KEY_LENGTH_SHIFT, layout->key_length_bits);
    const uint32_t result_bit = BIT(layout->result_shift);
    gcry_mpi_point_t point;
    gcry_ctx_t ctx;
    size_t len = 0;
    const char *name = esp_ecc_curve_name(curve, &len);

    if (name == NULL) {
        qemu_log_mask(LOG_UNIMP, "esp_ecc: curve %u is not modelled\n", curve);
    } else if (mode != ECC_MODE_POINT_MUL && mode != ECC_MODE_VERIFY &&
               mode != ECC_MODE_VERIFY_THEN_MUL) {
        qemu_log_mask(LOG_UNIMP, "esp_ecc: work mode %u is not modelled\n", mode);
    } else if (gcry_mpi_ec_new(&ctx, NULL, name) == 0) {
        point = esp_ecc_point_from_le(s->mem[PARAM_PX], s->mem[PARAM_PY], len);

        if (mode == ECC_MODE_POINT_MUL) {
            esp_ecc_point_mul(s, ctx, point, len);
        } else if (gcry_mpi_ec_curve_point(point, ctx)) {
            s->conf |= result_bit;
            if (mode == ECC_MODE_VERIFY_THEN_MUL) {
                esp_ecc_point_mul(s, ctx, point, len);
            }
        } else {
            s->conf &= ~result_bit;
        }

        gcry_mpi_point_release(point);
        gcry_ctx_release(ctx);
    }

    s->int_raw = true;
    esp_ecc_update_irq(s);
}

static uint64_t esp_ecc_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPEccState *s = ESP_ECC(opaque);
    const ESPEccLayout *layout = ESP_ECC_GET_CLASS(s)->layout;

    switch (addr) {
    case A_ECC_INT_RAW:
        return s->int_raw;
    case A_ECC_INT_ST:
        return s->int_raw && s->int_ena;
    case A_ECC_INT_ENA:
        return s->int_ena;
    case A_ECC_CONF:
        return s->conf;
    case A_ECC_DATE:
        return layout->date;
    default:
        if (addr >= A_ECC_MEM) {
            const hwaddr offset = addr - A_ECC_MEM;
            return s->mem[offset / layout->mem_stride][(offset % layout->mem_stride) / 4];
        }
        return 0;
    }
}

static void esp_ecc_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESPEccState *s = ESP_ECC(opaque);
    const ESPEccLayout *layout = ESP_ECC_GET_CLASS(s)->layout;
    const uint32_t result_bit = BIT(layout->result_shift);

    switch (addr) {
    case A_ECC_INT_ENA:
        s->int_ena = value & 1;
        esp_ecc_update_irq(s);
        break;
    case A_ECC_INT_CLR:
        if (value & 1) {
            s->int_raw = false;
            esp_ecc_update_irq(s);
        }
        break;
    case A_ECC_CONF:
        /* START and RESET clear themselves, and the result is read-only */
        s->conf = (s->conf & result_bit) |
                  (value & ~(result_bit | R_ECC_CONF_START_MASK | R_ECC_CONF_RESET_MASK));
        if (value & R_ECC_CONF_START_MASK) {
            esp_ecc_start(s);
        }
        break;
    default:
        if (addr >= A_ECC_MEM) {
            const hwaddr offset = addr - A_ECC_MEM;
            s->mem[offset / layout->mem_stride][(offset % layout->mem_stride) / 4] = value;
        }
        break;
    }
}

static const MemoryRegionOps esp_ecc_ops = {
    .read = esp_ecc_read,
    .write = esp_ecc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void esp_ecc_reset_hold(Object *obj, ResetType type)
{
    ESPEccState *s = ESP_ECC(obj);

    s->conf = 0;
    s->int_raw = false;
    s->int_ena = false;
    memset(s->mem, 0, sizeof(s->mem));
    qemu_irq_lower(s->irq);
}

static void esp_ecc_init(Object *obj)
{
    ESPEccState *s = ESP_ECC(obj);
    const ESPEccLayout *layout = ESP_ECC_GET_CLASS(s)->layout;
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_ecc_ops, s, TYPE_ESP_ECC,
                          A_ECC_MEM + layout->param_count * layout->mem_stride);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void esp_ecc_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp_ecc_reset_hold;
}

static void esp_ecc_chip_class_init(ObjectClass *klass, void *data)
{
    ESP_ECC_CLASS(klass)->layout = data;
}

/* The field positions are from soc/<chip>/register/soc/ecc_mult_reg.h in IDF. */
static const ESPEccLayout esp32c6_ecc_layout = {
    .key_length_bits = 1,
    .work_mode_shift = 5,
    .work_mode_bits = 3,
    .result_shift = 8,
    .param_count = 3,
    .mem_stride = 0x20,
    .date = 0x2201240,
};

static const ESPEccLayout esp32h2_ecc_layout = {
    .key_length_bits = 1,
    .work_mode_shift = 4,
    .work_mode_bits = 4,
    .result_shift = 29,
    .param_count = 6,
    .mem_stride = 0x20,
    .date = 0x2207180,
};

static const ESPEccLayout esp32c61_ecc_layout = {
    .key_length_bits = 1,
    .work_mode_shift = 4,
    .work_mode_bits = 4,
    .result_shift = 29,
    .param_count = 6,
    .mem_stride = 0x20,
    .date = 0x2401060,
};

/* ESP32-C5, ESP32-H4 and ESP32-P4 v3 */
static const ESPEccLayout esp_ecc_p384_layout = {
    .key_length_bits = 2,
    .work_mode_shift = 5,
    .work_mode_bits = 4,
    .result_shift = 29,
    .param_count = 6,
    .mem_stride = 0x30,
    .date = 0x2408120,
};

#define ESP_ECC_CHIP(chip_type, chip_layout) {  \
    .name = chip_type,                          \
    .parent = TYPE_ESP_ECC,                     \
    .class_init = esp_ecc_chip_class_init,      \
    .class_data = (void *)&chip_layout,         \
}

static const TypeInfo esp_ecc_types[] = {
    {
        .name = TYPE_ESP_ECC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(ESPEccState),
        .instance_init = esp_ecc_init,
        .class_init = esp_ecc_class_init,
        .class_size = sizeof(ESPEccClass),
        .abstract = true,
    },
    ESP_ECC_CHIP(TYPE_ESP32C5_ECC, esp_ecc_p384_layout),
    ESP_ECC_CHIP(TYPE_ESP32C6_ECC, esp32c6_ecc_layout),
    ESP_ECC_CHIP(TYPE_ESP32C61_ECC, esp32c61_ecc_layout),
    ESP_ECC_CHIP(TYPE_ESP32H2_ECC, esp32h2_ecc_layout),
    ESP_ECC_CHIP(TYPE_ESP32H4_ECC, esp_ecc_p384_layout),
    ESP_ECC_CHIP(TYPE_ESP32P4_ECC, esp_ecc_p384_layout),
};

DEFINE_TYPES(esp_ecc_types)

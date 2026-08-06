/*
 * ESP-PSRAM basic emulation
 *
 * Copyright (c) 2021-2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/misc/ssi_psram.h"

#define PSRAM_WARNING   0


typedef enum PsramCMD {
    NOP              = 0x00,
    READ             = 0x03,
    FAST_READ        = 0x0B,
    FAST_READ_QUAD   = 0xEB,
    WRITE            = 0x02,
    QUAD_WRITE       = 0x38,
    ENTER_QUAD_MODE  = 0x35,
    EXIT_QUAD_MODE   = 0xF5,
    RESET_ENABLE     = 0x66,
    RESET            = 0x99,
    SET_BURST_LENGTH = 0xC0,
    READ_ID          = 0x9F,

    /* Octal PSRAM commands */
    OCT_READ_REG     = 0x4040,
    OCT_WRITE_REG    = 0xc0c0,

    OCT_READ_SYNC    = 0x0000,
    OCT_WRITE_SYNC   = 0x8080,

    OCT_READ_LINEAR  = 0x2020,
    OCT_WRITE_LINEAR = 0xA0A0,
} PsramCMD;

/* If the manufacturer ID is 0xd, all the 8MB PSRAMs are in fact 4MB underneath
 * So use another manufacturer ID. */
#define PSRAM_ID_MFG 0xff
#define PSRAM_ID_KGD 0x5d

#define MR0_GET_RD_LATENCY(v)       (((v) >> 2) & 0x7)

#define MR0_DRIVE_STRENGHT_HALF     ((uint8_t)0b01  << 0)
#define MR0_RD_LATENCY_CODE         ((uint8_t)0b010 << 2)
#define MR0_RD_LT_VARIABLE          ((uint8_t)0b0   << 5)

#define MR1_VENDOR_ID               ((uint8_t)0b01101 << 0)
#define MR1_NO_ULP                  ((uint8_t)0b0     << 5)

#define MR2_DENSITY_MASK            ((uint8_t)0b111   << 0)
#define MR2_DEVICE_ID_3_GEN         ((uint8_t)0b10    << 3)
#define MR2_GOOD_DIE_BIT_PASS       ((uint8_t)0b1     << 7)

#define MR3_SRF_FAST_REFRESH        ((uint8_t)0b1     << 5)
#define MR3_OP_VOLTAGE_1V8          ((uint8_t)0b0     << 6)
#define MR3_RBX_NOT_SUPPORTED       ((uint8_t)0b0     << 7)

#define MR4_PASR_64MB               ((uint8_t)0b000   << 0)
#define MR4_FAST_REFRESH            ((uint8_t)0b0     << 3)
#define MR4_WRITE_LATENCY_5         ((uint8_t)0b010   << 4)

#define MR6_ULP_HALF_SLEEP          ((uint8_t)0xF0    << 0)

#define MR8_32BYTE_BURST            ((uint8_t)0b01    << 0)
#define MR8_HYBRID_BURST            ((uint8_t)0b1     << 2)
#define MR8_RBX_READ_DISABLE        ((uint8_t)0b0     << 3)

/**
 * Since the DQS line is not emulated, we have to set these values according to
 * the default configuration, which is also used by IDF
 */
#define OCT_PSRAM_RD_DUMMY  3
#define OCT_PSRAM_WR_DUMMY  1

#define FAKE_16MB_ID    0x6a
#define FAKE_32MB_ID    0x8e

static int get_eid_by_size(uint32_t size_mbytes) {
    switch (size_mbytes)
    {
    case 2:
        return 0x00;
    case 4:
        return 0x21;
    case 8:
        return 0x40;
    case 16:
        return FAKE_16MB_ID;
    case 32:
        return FAKE_32MB_ID;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: PSRAM size %" PRIu32 "MB not implemented\n",
                      __func__, size_mbytes);
        return -1;
    }
}


/**
 * @brief Check if the current command is a write command
 */
static inline bool psram_is_write_command(SsiPsramState *s)
{
    return s->command == WRITE || s->command == QUAD_WRITE ||
           s->command == OCT_WRITE_SYNC || s->command == OCT_WRITE_LINEAR;
}

/**
 * @brief Check if the current command is a read command
 */
static inline bool psram_is_read_command(SsiPsramState *s)
{
    return s->command == READ || s->command == FAST_READ || s->command == FAST_READ_QUAD ||
           s->command == OCT_READ_SYNC || s->command == OCT_READ_LINEAR;
}

/**
 * @brief Write data to the PSRAM's internal RAM.
 * The address will be taken from the `addr` field added to the `offset`.
 */
static void psram_write_data(SsiPsramState *s, off_t offset, uint8_t byte)
{
    uint8_t* ptr = (uint8_t*) memory_region_get_ram_ptr(&s->data_mr);
    const uint32_t size_bytes = s->size_mbytes * 1024 * 1024;
    off_t destination = s->addr + offset;
    if (destination >= 0 && destination < (off_t)size_bytes) {
        ptr[destination] = byte;
    }
}


/**
 * @brief Read data from the PSRAM's internal RAM.
 * The address will be taken from the `addr` field added to the `offset`.
 */
static uint8_t psram_read_data(SsiPsramState *s, off_t offset)
{
    uint8_t* ptr = (uint8_t*) memory_region_get_ram_ptr(&s->data_mr);
    const uint32_t size_bytes = s->size_mbytes * 1024 * 1024;
    off_t destination = s->addr + offset;
    if (destination >= 0 && destination < (off_t)size_bytes) {
        return ptr[destination];
    }
    return 0;
}

static PsramState psram_quad_write_idle(SsiPsramState *s, uint32_t value)
{
    PsramState next_state = s->state;
    /* Idle state, check if a new command is sent */
    switch (value) {
        case NOP:
            break;
        case READ_ID:
            /* Should already be 0 but let's be safe */
            s->byte_count = 0;
            next_state = ST_READ_ID;
            break;
        case WRITE:
        case QUAD_WRITE:
        case READ:
        case FAST_READ:
        case FAST_READ_QUAD:
            s->command = value;
            s->byte_count = 0;
            next_state = ST_CMD_READY;
            break;
        default:
#if PSRAM_WARNING
            warn_report("\x1b[31m[QUAD PSRAM] Unsupported command 0x%02x \x1b[0m\n", value);
#endif
            break;
    }
    return next_state;
}

/**
 * @brief Simulate a byte write on the PSRAM, returns the next state the PSRAM should be
 * put in AFTER performing the associated `read`.
 */
static PsramState psram_quad_write(SsiPsramState *s, uint32_t value)
{
    /* By default, the state doens't change */
    PsramState next_state = s->state;
    switch (s->state) {
        case ST_IDLE:
            next_state = psram_quad_write_idle(s, value);
            break;
        case ST_CMD_READY:
            /* Received the (valid) command */
            s->addr = value;
            next_state = ST_CMD_ADDR0;
            break;
        case ST_CMD_ADDR0:
            s->addr = (s->addr << 8) | value;
            next_state = ST_CMD_ADDR1;
            break;
        case ST_CMD_ADDR1:
            s->addr = (s->addr << 8) | value;
            /* Only 3 bytes (24-bit) addresses on QSPI PSRAM */
            next_state = ST_PROCESSING;
            break;
        case ST_PROCESSING:
            if (psram_is_write_command(s)) {
                /* Only increment the byte_count if we are in a write command, else, the
                 * `psram_quad_read` function is responsible for incrementing it */
                psram_write_data(s, s->byte_count++, value);
            }
            break;
        case ST_READ_ID:
        default:
            /* In transaction state, keep track of the number of bytes transferred */
            s->byte_count++;
            break;
    }
    return next_state;
}

static uint32_t psram_quad_read(SsiPsramState *s)
{
    uint32_t result = 0;

    if (s->state == ST_READ_ID) {
        const uint8_t read_id_response[] = {
            /* 1 byte for the command itself, 3 bytes for the address */
            0x00, 0x00, 0x00, 0x00,
            PSRAM_ID_MFG, PSRAM_ID_KGD,
            get_eid_by_size(s->size_mbytes),
            0xaa, 0xbb, 0xcc, 0xdd, 0xee
        };
        const int index = s->byte_count;
        if (index < ARRAY_SIZE(read_id_response)) {
            result = read_id_response[index];
        }
    } else if (s->state == ST_PROCESSING && psram_is_read_command(s)) {
        result = psram_read_data(s, s->byte_count++);
    }
    return result;
}



static bool psram_octal_supported_commands(uint32_t command)
{
    switch (command) {
        case OCT_READ_REG:
        case OCT_WRITE_REG:
        case OCT_READ_SYNC:
        case OCT_WRITE_SYNC:
        case OCT_READ_LINEAR:
        case OCT_WRITE_LINEAR:
            return true;
        default:
            return false;
    }
}


static uint32_t psram_octal_read(SsiPsramState *s)
{
    uint32_t result = 0;

    if (s->state == ST_PROCESSING && s->command == OCT_READ_REG) {
        // Odd read bytes correspond to the next register
        switch (s->addr & 0xff) {
        case 0:
            result = (s->byte_count % 2) ? s->mr1 : s->mr0;
            break;
        case 1:
            result = (s->byte_count % 2) ? s->mr2 : s->mr1;
            break;
        case 2:
            result = (s->byte_count % 2) ? s->mr3 : s->mr2;
            break;
        case 3:
            result = (s->byte_count % 2) ? s->mr4 : s->mr3;
            break;
        case 4:
            result = (s->byte_count % 2) ? s->mr8 : s->mr4;
            break;
        case 8:
            result = (s->byte_count % 2) ? s->mr0 : s->mr8;
            break;
        default:
            // Should not happen
            break;
        }
        s->byte_count++;
    } else if (s->state == ST_PROCESSING && psram_is_read_command(s)) {
        result = psram_read_data(s, s->byte_count++);
    }
    return result;
}


static PsramState psram_octal_write(SsiPsramState *s, uint32_t value)
{
    PsramState next_state = s->state;

    switch (s->state) {
        case ST_IDLE:
            s->command = value;
            next_state = ST_CMD_LSB;
            break;
        case ST_CMD_LSB:
            s->command |= value << 8;
            if (psram_octal_supported_commands(s->command)) {
                next_state = ST_CMD_READY;
            } else {
#if PSRAM_WARNING
                if (s->command != 0) {
                    warn_report("\x1b[31m[OCT PSRAM] Unsupported command 0x%04x \x1b[0m\n", value);
                }
#endif
                next_state = ST_IDLE;
            }
            break;
        case ST_CMD_READY:
            /* Received the (valid) command */
            s->addr = value;
            next_state = ST_CMD_ADDR0;
            break;
        case ST_CMD_ADDR0:
            s->addr = (s->addr << 8) | value;
            next_state = ST_CMD_ADDR1;
            break;
        case ST_CMD_ADDR1:
            s->addr = (s->addr << 8) | value;
            next_state = ST_CMD_ADDR2;
            break;
        case ST_CMD_ADDR2:
            s->addr = (s->addr << 8) | value;
            /* Address was received, process data */
            /* Reading and writing registers don't introdue a dummy cycle requirement */
            if (s->command == OCT_READ_REG || psram_is_write_command(s)) {
                /* Only a single dummy byte in write mode and registers read mode */
                next_state = ST_DUMMY_CYCLE;
                s->dummy_cycles = OCT_PSRAM_WR_DUMMY;
            } else if (s->command == OCT_WRITE_REG) {
                next_state = ST_PROCESSING;
            } else {
                /* Read command */
                next_state = ST_DUMMY_CYCLE;
                s->dummy_cycles = OCT_PSRAM_RD_DUMMY;
            }
            break;
        case ST_DUMMY_CYCLE:
            s->dummy_cycles--;
            if (s->dummy_cycles == 0) {
                next_state = ST_PROCESSING;
            }
            break;
        case ST_PROCESSING:
            /* Register write only takes into account the first byte, ignores the rest */
            if (s->command == OCT_WRITE_REG && s->byte_count == 0) {
                switch (s->addr) {
                case 0:
                    /* Check the latency bits */
                    if (MR0_GET_RD_LATENCY(value) != 2) {
                        warn_report("\x1b[31m[OCT PSRAM] Read Latency %d unsupported\x1b[0m\n", MR0_GET_RD_LATENCY(value));
                    }
                    s->mr0 = value;
                    break;
                case 4:
                    s->mr4 = value;
                    break;
                case 8:
                    s->mr8 = value;
                    break;
                }
                s->byte_count++;
            } else if (psram_is_write_command(s)) {
                /* Only increment the byte_count if we are in a write command, else, the
                 * `read` function is responsible for incrementing it */
                psram_write_data(s, s->byte_count++, value);
            }
            break;

        default:
            break;
    }

    return next_state;
}


static int psram_octal_get_density(uint32_t size_mbytes)
{
    int density = 0;

    /* These density values were taken from ESP-IDF Octal PSRAM driver */
    switch (size_mbytes) {
    case 4:
        density = 1;
        break;
    case 8:
        density = 3;
        break;
    case 16:
        density = 5;
        break;
    case 32:
        density = 7;
        break;
    case 64:
        density = 6;
        break;
    default:
        error_report("[PSRAM] Invalid size %dMB for octal PSRAM\n", size_mbytes);
        break;
    }

    return density & MR2_DENSITY_MASK;
}


static uint32_t psram_transfer(SSIPeripheral *dev, uint32_t value)
{
    SsiPsramState *s = SSI_PSRAM(dev);
    PsramState next_state;
    uint32_t data;

    if (s->is_octal) {
        next_state = psram_octal_write(s, value);
        data = psram_octal_read(s);
    } else {
        next_state = psram_quad_write(s, value);
        data = psram_quad_read(s);
    }

    /* Set the new state AFTER calling read */
    s->state = next_state;
    return data;
}

static int psram_cs(SSIPeripheral *ss, bool select)
{
    SsiPsramState *s = SSI_PSRAM(ss);

    if (!select) {
        /* If data were written to the cache via the MemoryRegion, we need to
         * mark the area as dirty since the ESP target's `cache` also uses it. */
        if (s->state == ST_PROCESSING && psram_is_write_command(s)) {
            memory_region_set_dirty(&s->data_mr, s->addr, s->byte_count);
        }
        s->state = ST_IDLE;
        s->byte_count = 0;
        s->command = -1;
        s->addr = -1;
        s->dummy_cycles = 0;
    }
    return 0;
}

static void psram_realize(SSIPeripheral *ss, Error **errp)
{
    SsiPsramState *s = SSI_PSRAM(ss);

    if (s->is_octal) {
        /* Set the default MR values for the octal psram (ref: Datasheet APS6408L_OBMx) */
        s->mr0 = MR0_RD_LT_VARIABLE | MR0_RD_LATENCY_CODE | MR0_DRIVE_STRENGHT_HALF;
        s->mr1 = MR1_NO_ULP | MR1_VENDOR_ID;
        s->mr2 = MR2_GOOD_DIE_BIT_PASS | MR2_DEVICE_ID_3_GEN | psram_octal_get_density(s->size_mbytes);
        s->mr3 = MR3_RBX_NOT_SUPPORTED | MR3_OP_VOLTAGE_1V8 | MR3_SRF_FAST_REFRESH;
        s->mr4 = MR4_WRITE_LATENCY_5 | MR4_FAST_REFRESH | MR4_PASR_64MB;
        s->mr8 = MR8_RBX_READ_DISABLE | MR8_HYBRID_BURST | MR8_32BYTE_BURST;
    } else if (get_eid_by_size(s->size_mbytes) == -1) {
        error_report("[PSRAM] Invalid size %dMB for QUAD PSRAM", s->size_mbytes);
    }

    /* Allocate the actual array that will act as a vritual RAM */
    const uint32_t size_bytes = s->size_mbytes * 1024 * 1024;
    memory_region_init_ram(&s->data_mr, OBJECT(s), "psram.memory_region", size_bytes, &error_fatal);

    s->state = ST_IDLE;
}

static Property psram_properties[] = {
    DEFINE_PROP_BOOL("is_octal", SsiPsramState, is_octal, false),
    DEFINE_PROP_UINT32("size_mbytes", SsiPsramState, size_mbytes, 4),
    DEFINE_PROP_END_OF_LIST(),
};


static void psram_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->transfer = psram_transfer;
    k->set_cs = psram_cs;
    k->cs_polarity = SSI_CS_LOW;
    k->realize = psram_realize;
    device_class_set_props(dc, psram_properties);
}

static const TypeInfo psram_info = {
    .name          = TYPE_SSI_PSRAM,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(SsiPsramState),
    .class_init    = psram_class_init
};

static void psram_register_types(void)
{
    type_register_static(&psram_info);
}

type_init(psram_register_types)

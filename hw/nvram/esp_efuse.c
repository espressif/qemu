/*
 * eFuse emulation for recent ESP32-series chip (ESP32-S3 and newer)
 *
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "chardev/char-fe.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/nvram/esp_efuse.h"


#define EFUSE_DEBUG    0

/**
 * @brief Specify the delay, in us of a a write or read operation, this will only be used to simulate
 * the delay the real efuses actually take on real hardware.
 */
#define EFUSE_OPERATION_DELAY_US    100


/**
 * Magic values for write and read operations
 */
#define EFUSE_WRITE_OPCODE  0x5A5A
#define EFUSE_READ_OPCODE   0x5AA5


/**
 * Define a few helpers for the efuse blocks
 */

/**
 * Returns the size of the given block, in bytes.
 * BLOCK0 and BLOCK1 have 6 registers while other blocks have 8 registers
 */
static inline int esp_efuse_block_size(const uint32_t block)
{
    return (block == 0 || block == 1) ? ESP_EFUSE_BLOCK0_WORDS * sizeof(uint32_t)
                                      : 8 * sizeof(uint32_t);
}

/**
 * Returns true if the current command is a valid read command, false else
 */
static inline bool esp_efuse_is_read_cmd(ESPEfuseState *s)
{
    return FIELD_EX32(s->efuses.conf, EFUSE_CONF, OP_CODE) == EFUSE_READ_OPCODE &&
           FIELD_EX32(s->efuses.cmd, EFUSE_CMD, READ) != 0;
}

/**
 * Returns true if the current command is a valid write/program command, false else
 */
static inline bool esp_efuse_is_write_cmd(ESPEfuseState *s)
{
    return FIELD_EX32(s->efuses.conf, EFUSE_CONF, OP_CODE) == EFUSE_WRITE_OPCODE &&
           FIELD_EX32(s->efuses.cmd, EFUSE_CMD, PGM) != 0;
}


/**
 * @brief Return the start offset, in bytes, of the given block in the ESPEfuseBlocks structure.
 */
static int esp_offset_of_block(int block_num)
{
    /* The alst block is block 10 */
    assert(block_num <= 10);

    int word_offset = 0;

    if (block_num > 0) {
        word_offset += ESP_EFUSE_BLOCK0_WORDS;
    }

    if (block_num > 1) {
        /* Skip block 1 and add the remaining blocks, all 8 words big */
        word_offset += ESP_EFUSE_BLOCK1_WORDS + (block_num - 2) * 8;
    }

    return word_offset * sizeof(uint32_t);
}


/**
 * @brief Hide the protected efuses by overwriting them with 0s, this function shall be called after
 * calling `esp_efuse_read`.
 */
static void esp_hide_protected_block(ESPEfuseState *s)
{
    uint32_t rd_protection = FIELD_EX32(s->efuses.blocks.rd_repeat_data0, EFUSE_RD_REPEAT_DATA0, RD_DIS);
    /* Only the BLOCK4 and upwards can be protected */
    int block_num = 4;

    while (rd_protection != 0) {
        if (rd_protection & 1) {
            /* Get the offset of the block in the ESPEfuseRegs structure */
            const int offset = esp_offset_of_block(block_num);
            uint8_t* from = ((uint8_t*) &s->efuses.blocks) + offset;
            /* The blocks that can be protected are all 32-byte long */
            memset(from, 0, 32);
        }
        rd_protection >>= 1;
        block_num++;
    }
}


/**
 * @brief Load the efuses value from the block device (file)
 */
static void esp_efuse_reload_from_blk(ESPEfuseState *s)
{
    const uint32_t size = sizeof(ESPEfuseBlocks);

    /* Load the efuses from the block device file (or mirror) */
    if (s->blk) {

        /* Load the file content inside the structure, starting at efuse rd_wr_dis */
        const int ret = blk_pread(s->blk, 0, size, &s->efuses.blocks, 0);
        if (ret < 0) {
            error_report("%s: failed to read the block device (%d)", __func__, ret);
        }

    } else {

        assert(s->mirror);
        memcpy(&s->efuses.blocks, s->mirror, size);
    }

    /* Copy the efuses to the internal mirror */
    memcpy(&s->efuses_internal.blocks, &s->efuses.blocks, size);
}

/**
 * @brief Get a mask of the protected bits for BLOCK0.
 * A bit set to 1 marks a protected bit whereas a 0 marks an unprotected bit.
 *
 * @param wr_dis Write-disable register
 * @param block0_mask Mask containing exactly ESP_EFUSE_BLOCK0_WORDS words that will
 *                    be filled with the masks described above.
 */
static void esp_efuse_get_block0_protected_mask(uint32_t wr_dis, uint32_t *block0_mask)
{
    /* Define the constants that for each bit of wr_dis represent the word index they affect
     * and the bits they protect */
    const struct {
        uint32_t index;
        uint32_t mask;
    } protect_map[32] = {
        [0]  = { 1, 0x0000007f },    /* Bit 0: protects rd_repeat_data0 (index 1), rd_dis bits */
        /* Bit 1 unused */
        [2]  = { 1, 0x0018df00 },
        [3]  = { 2, 0x00030000 },
        [4]  = { 2, 0x001c0000 },
        [5]  = { 2, 0x00200000 },
        [6]  = { 2, 0x00400000 },
        [7]  = { 2, 0x00800000 },
        [8]  = { 2, 0x0f000000 },
        [9]  = { 2, 0xf0000000 },
        [10] = { 3, 0x0000000f },
        [11] = { 3, 0x000000f0 },
        [12] = { 3, 0x00000f00 },
        [13] = { 3, 0x0000f000 },
        /* Bit 14 unused */
        [15] = { 3, 0x00100000 },
        [16] = { 3, 0x00200000 },
        /* Bit 17 unused */
        [18] = { 4, 0x3fffe0f5 }, /* TODO: Make sure that bit 18 also protects EFUSE_FLASH_TPUW, omitted for now */
        [19] = { 4, 0x80000000 },
        /* bit 20 to 29 included don't affect BLOCK0 fields */
        [30] = { 1, 0x06000000 },
        [31] = { 1, 0x00070000 },
    };

    memset(block0_mask, 0, ESP_EFUSE_BLOCK0_WORDS * sizeof(uint32_t));

    /* Go through all the bits of the write-disable mask and set the appropriate mask if the bit
     * is set. Ignore bits from 20 to 29 included which are not about BLOCK 0 protection. */
    for (uint_fast32_t i = 0; i <= 31; i++) {
        if ((i >= 20 && i <= 29) || (wr_dis & BIT(i)) == 0) {
            continue;
        }
        /* Bit is set and within range */
        const uint32_t index = protect_map[i].index;
        const uint32_t mask = protect_map[i].mask;
        block0_mask[index] |= mask;
    }
}


/**
 * @brief Write a given efuses block to the block device (file) if not protected.
 * Returns true if the block was flashed successfully, false else.
 */
static bool esp_efuse_write_to_blk(ESPEfuseState *s, const int block)
{
    const int size =  esp_efuse_block_size(block);
    bool protected = false;
    /* Mask of protected bit for each word of a BLOCK, only used when writing BLOCK0 */
    uint32_t block_mask[ESP_EFUSE_PGM_DATA_COUNT] = { 0 };

    /* If the block to protect is not BLOCK0 the check is rather simple */
    if (block != 0) {
        assert(block <= 10);
        /* BLOCK1 protection is bit 20, BLOCK2 protection is bit 21, etc... */
        const int offset = 19 + block;
        /* If the bit is 1, protection is enabled, we cannot write */
        protected = (s->efuses.blocks.rd_wr_dis >> offset) & 1;
    } else {
        /* BLOCK0 protection is done on a bit granularity, so for each word that composes it
         * get mask where 1 represents a protected bit, and 0 represents an unprotected bit */
        esp_efuse_get_block0_protected_mask(s->efuses.blocks.rd_wr_dis, block_mask);
    }

    if (!protected) {
        /* Get the offset of the block in the ESPEfuseRegs structure!
         * Subtract the offset of the BLOCK0 to get the offset of our block in the
         * binary file (blk).
         * The offset in struct must be in 32-bit words */
        const uint32_t offset_in_struct = esp_offset_of_block(block) / sizeof(uint32_t);
        /* Offset in file must be in bytes */
        const uint32_t offset_in_file = esp_offset_of_block(block);

        /* Generate the actual data to write to the file. Indeed, the programmed bits (1) shall
         * NOT be programmed to 0 as on real hardware an efuse cannot be reverted.
         * To do so, OR the content to burn with the existing content so that the 1s are never erased. */
        uint32_t *efuses = (uint32_t*) &s->efuses.blocks;
        uint32_t *new_data = s->efuses.pgm_data;
        uint32_t real_data[ESP_EFUSE_PGM_DATA_COUNT];

        for (int i = 0; i < ESP_EFUSE_PGM_DATA_COUNT; i++) {
            /* Offset of pgm_data is 0, let's use efuses[i] to retrieve the data.
             * efuses[i] represents the new value, efuses[offset_in_struct + i] represents the old value,
             * block_mask represents the protection, with 1 marking a bit as protected.
             * As such, the final result of an efuse value is:
             * Y = old_value | (~block_mask & new_value) */
            real_data[i] = efuses[offset_in_struct + i] | (~block_mask[i] & new_data[i]);
        }

        /* Write the new block data to the file (or RAM) */
        if (s->blk) {

            const int ret = blk_pwrite(s->blk, offset_in_file, size, real_data, 0);
            if (ret < 0) {
                error_report("%s: failed to write efuses to the block device (%d)", __func__, ret);
            }

        } else {

            assert(s->mirror);
            memcpy((uint8_t*) s->mirror + offset_in_file, real_data, size);

        }
    }

    /* Writing is a success if the block is not protected */
    return !protected;
}


/**
 * @brief Callback called when the QEMU timer reaches its limit.
 * It will set the raw status of the efuse component. If the requested operation was a read,
 * it will perform the read from the binary file (blk) here.
 */
static void esp_efuse_timer_cb(void *opaque)
{
    ESPEfuseState *s = ESP_EFUSE(opaque);

    /* To make sure the command register did not change between the moment the operation was scheduled
     * and the moment the callback was triggered, restore its value to its original one. (mirror)
     * In any case, it will be set to 0 at the end of this function */
    s->efuses.cmd = s->op_cmd_mirror;

    /* No need to check the opcode again */
    if (FIELD_EX32(s->efuses.cmd, EFUSE_CMD, READ) != 0) {
        esp_efuse_reload_from_blk(s);
        esp_hide_protected_block(s);
        s->efuses.int_raw |= R_EFUSE_INT_RAW_READ_DONE_MASK;
    } else {
        assert(FIELD_EX32(s->efuses.cmd, EFUSE_CMD, PGM) != 0);
        s->efuses.int_raw |= R_EFUSE_INT_RAW_PGM_DONE_MASK;
    }

    /* In any case, reset the command register to show that the operation is finished */
    s->efuses.cmd = 0;

    /* Set the interrupt bits, if any is 1, trigger an interrupt */
    s->efuses.int_st = s->efuses.int_ena & s->efuses.int_raw;
    if (s->efuses.int_st) {
        qemu_irq_raise(s->irq);
    }
}


/**
 * @brief Start ESPEfuseState's timer to simulate the efuse operation delay
 */
static void esp_efuse_op_timer_start(ESPEfuseState *s)
{
    const uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const uint64_t interval_ns = EFUSE_OPERATION_DELAY_US * 1000;
    timer_mod_anticipate_ns(&s->op_timer, ns_now + interval_ns);
}

static uint64_t esp_efuse_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPEfuseState *s = ESP_EFUSE(opaque);

#if EFUSE_DEBUG
    info_report("[EFUSE] Reading 0x%08lx (size: %d)", addr, size);
#endif

    /* Check if the programming cmd block is being written */
    if (addr <= A_EFUSE_RD_SYS_PART2_DATA7_REG) {
        /* Set the base to the first fields of Efuse hardware module: PGM data 0 */
        uint8_t* base = ((uint8_t*) (s->efuses.pgm_data)) + addr;

        if (addr >= A_EFUSE_RD_WR_DIS_REG) {
            /* If the read is done on an efuse block, change the base */
            base = ((uint8_t*) (&s->efuses.blocks.rd_wr_dis)) + addr - A_EFUSE_RD_WR_DIS_REG;
        }

        if (size == 1) {
            return *((uint8_t*) base);
        } else if (size == 2) {
            return *((uint16_t*) base);
        } else {
            return *((uint32_t*) base);
        }
    }

    /* Treat the interrupt-related cases separately, make sure we only access the registers below as 32-bit words */
    assert(size >= 4);

    switch (addr) {
        case A_EFUSE_CMD:
            return s->efuses.cmd;
        case A_EFUSE_INT_ST:
            return s->efuses.int_st;
        case A_EFUSE_INT_RAW:
            return s->efuses.int_raw;
        case A_EFUSE_INT_ENA:
            return s->efuses.int_ena;
        case A_EFUSE_CLK:
            return s->efuses.clk;
        case A_EFUSE_CONF:
            return s->efuses.conf;
        case A_EFUSE_DAC_CONF:
            return s->efuses.dac_conf;
        case A_EFUSE_RD_TIM_CONF:
            return s->efuses.rd_tim_conf;
        case A_EFUSE_WR_TIM_CONF1:
            return s->efuses.wr_tim_conf1;
        case A_EFUSE_WR_TIM_CONF2:
            return s->efuses.wr_tim_conf2;
        case A_EFUSE_STATUS:
            return s->efuses.status;
        /* The other registers are read-only */
        default:
            return 0;
    }
}


static void esp_efuse_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESPEfuseState *s = ESP_EFUSE(opaque);

#if EFUSE_DEBUG
    info_report("[EFUSE] Writing to 0x%08lx = 0x%08lx (size: %d)", addr, value, size);
#endif

    /* Check if the programming cmd block is being written */
    if (addr == A_EFUSE_CMD) {

        s->efuses.cmd = (uint32_t) value;

        /* The command bit and the opcode will be check by the underlying function */
        if (esp_efuse_is_read_cmd(s)) {

            /* Save the register value and schedule a timer to simulate real efuse loading delay,
             * the copy will be done in the callback. */
            s->op_cmd_mirror = (uint32_t) value;
            esp_efuse_op_timer_start(s);

        } else if (esp_efuse_is_write_cmd(s)) {

            const uint32_t blk_num = FIELD_EX32(s->efuses.cmd, EFUSE_CMD, BLK_NUM);
            const bool success = esp_efuse_write_to_blk(s, blk_num);

            if (success) {
                /* Same as for the read, schedule the timer and perform the actual transfer once it elapsed */
                s->op_cmd_mirror = (uint32_t) value;
                esp_efuse_op_timer_start(s);
            } else {
                s->efuses.cmd = 0;
                s->op_cmd_mirror = 0;
            }
        } else {
            s->efuses.cmd = 0;
        }

        return;
    } else if (addr <= A_EFUSE_PGM_CHECK_VALUE2_REG) {
         /* The first PGM registers can be written freely */
        void* pgm = ((uint8_t*) (s->efuses.pgm_data)) + addr;

        if (size == 1) {
            *((uint8_t*) pgm) = value & 0xff;
        } else if (size == 2) {
            *((uint16_t*) pgm) = value & 0xffff;
        } else {
            *((uint32_t*) pgm) = value;
        }

        return;
    }

    /* Treat the interrupt-related cases separately, make sure we only access the registers below as 32-bit words */
    assert(size >= 4);

    switch (addr) {
        case A_EFUSE_INT_CLR:
            /* Only clear the bits that are set to 1 in the value */
            s->efuses.int_raw &= ((~value) & 0b11);
            break;
        case A_EFUSE_INT_RAW:
            s->efuses.int_raw = 0;
            break;
        case A_EFUSE_INT_ENA:
            s->efuses.int_ena = value & 0b11;
            break;

        /* For debugging purposes */
        case A_EFUSE_DBG_ERASE_ALL:
            {
#if 1
                info_report("[EFUSE] erasing all efuses!");
#endif
                const uint32_t fuse_size = sizeof(ESPEfuseBlocks);
                memset(&s->efuses.blocks, 0, fuse_size);

                if (s->blk) {
                    int ret = blk_pwrite(s->blk, 0, fuse_size, &s->efuses.blocks, 0);
                    if (ret != 0) {
                        error_report("ERROR WRITING FILE: %d\n", ret);
                        exit(1);
                    }
                } else {
                    assert(s->mirror);
                    memset(s->mirror, 0, fuse_size);
                }
            }
            return;

        case A_EFUSE_CLK:
            s->efuses.clk = value;
            return;
        case A_EFUSE_CONF:
            s->efuses.conf = value;
            return;
        case A_EFUSE_DAC_CONF:
            s->efuses.dac_conf = value;
            return;
        case A_EFUSE_RD_TIM_CONF:
            s->efuses.rd_tim_conf = value;
            return;
        case A_EFUSE_WR_TIM_CONF1:
            s->efuses.wr_tim_conf1 = value;
            return;
        case A_EFUSE_WR_TIM_CONF2:
            s->efuses.wr_tim_conf2 = value;
            return;
        /* The other registers are read-only */
        default:
            return;
    }

    /* Check if any interrupt needs to be triggered or, on the contrary, lowered */
    s->efuses.int_st = (s->efuses.int_ena & s->efuses.int_raw) & 0b11;

    qemu_set_irq(s->irq, s->efuses.int_st ? 1 : 0);
}


/**
 * @brief The following functions are virtual method part of the `ESPEfuseClass` structure.
 * They can be overriden by any child class.
 */
static uint32_t virt_efuse_dis_download_manual_encrypt(ESPEfuseState *s)
{
    return FIELD_EX32(s->efuses.blocks.rd_repeat_data0, EFUSE_RD_REPEAT_DATA0, DIS_DOWNLOAD_MANUAL_ENCRYPT) != 0;
}

static uint32_t virt_efuse_get_spi_boot_crypt_cnt(ESPEfuseState *s)
{
    return FIELD_EX32(s->efuses.blocks.rd_repeat_data1, EFUSE_RD_REPEAT_DATA1, SPI_BOOT_CRYPT_CNT) != 0;
}

/**
 * @brief Returns the key purpose of the given efuse key block number
 */
static uint32_t virt_efuse_get_key_purpose(ESPEfuseState *s, EfuseBlocksIdx efuse_block_num)
{
    uint32_t purpose = -1;
    switch (efuse_block_num) {
        case EFUSE_BLOCK_KEY0:
            purpose = FIELD_EX32(s->efuses.blocks.rd_repeat_data1, EFUSE_RD_REPEAT_DATA1, KEY_PURP_0);
            break;
        case EFUSE_BLOCK_KEY1:
            purpose = FIELD_EX32(s->efuses.blocks.rd_repeat_data1, EFUSE_RD_REPEAT_DATA1, KEY_PURP_1);
            break;
        case EFUSE_BLOCK_KEY2:
            purpose = FIELD_EX32(s->efuses.blocks.rd_repeat_data2, EFUSE_RD_REPEAT_DATA2, KEY_PURP_2);
            break;
        case EFUSE_BLOCK_KEY3:
            purpose = FIELD_EX32(s->efuses.blocks.rd_repeat_data2, EFUSE_RD_REPEAT_DATA2, KEY_PURP_3);
            break;
        case EFUSE_BLOCK_KEY4:
            purpose = FIELD_EX32(s->efuses.blocks.rd_repeat_data2, EFUSE_RD_REPEAT_DATA2, KEY_PURP_4);
            break;
        case EFUSE_BLOCK_KEY5:
            purpose = FIELD_EX32(s->efuses.blocks.rd_repeat_data2, EFUSE_RD_REPEAT_DATA2, KEY_PURP_5);
            break;
        default:
            error_report("[Efuse] Out of range key block specified: %d", efuse_block_num);
            break;
    }

    return purpose;
}


static bool virt_efuse_get_key(ESPEfuseState *s, EfuseBlocksIdx efuse_block_num, uint8_t* efuse_key)
{
    if (efuse_block_num < EFUSE_BLOCK_KEY0 || efuse_block_num > EFUSE_BLOCK_KEY6) {
        error_report("[Efuse] Out of range key block specified: %d", efuse_block_num);
        return false;
    }

    /* Interpret the key0 block address as an array of bytes */
    uint8_t* block_key0 = (uint8_t*) &s->efuses_internal.blocks.rd_key0_data0;
    memcpy(efuse_key, block_key0 + (efuse_block_num - EFUSE_BLOCK_KEY0) * 32, 32);
    return true;
}


static const MemoryRegionOps esp_efuse_ops = {
    .read =  esp_efuse_read,
    .write = esp_efuse_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp_efuse_reset_hold(Object *obj, ResetType type)
{
    ESPEfuseState *s = ESP_EFUSE(obj);
    timer_del(&s->op_timer);
    qemu_irq_lower(s->irq);
    esp_efuse_reload_from_blk(s);
    esp_hide_protected_block(s);
}

static void esp_efuse_realize(DeviceState *dev, Error **errp)
{
    ESPEfuseState *s = ESP_EFUSE(dev);
    const char* error_msg = NULL;

    /* If no file was given as efuses, create a temporary one (in RAM). */
    if (s->blk == NULL) {
        const uint32_t size = sizeof(ESPEfuseBlocks);
        s->mirror = g_malloc(size);
        if (s->mirror == NULL) {
            error_msg = "failed to allocate memory for efuses";
            goto error;
        }
        memset(s->mirror, 0, size);
    } else {
        /* A block was given as a parameter, open it in READ/WRITE */
        if (!blk_supports_write_perm(s->blk)) {
            error_msg = "block device is not writeable or does not exist";
            goto error;
        }

        uint64_t perm = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE;
        int ret = blk_set_perm(s->blk, perm, BLK_PERM_ALL, NULL);
        if (ret != 0) {
            error_msg = "failed to set permission";
            goto error;
        }

        esp_efuse_reset_hold(OBJECT(s), RESET_TYPE_COLD);
    }

    /* State machine is ready */
    s->efuses.status = FIELD_DP32(0, EFUSE_STATUS, STATE, 1);

    return;
error:
    error_setg(errp, "%s: %s", __func__, error_msg);
}

static void esp_efuse_init(Object *obj)
{
    ESPEfuseState *s = ESP_EFUSE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_efuse_ops, s,
                          TYPE_ESP_EFUSE, ESP_EFUSE_IO_RANGE_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->op_timer, QEMU_CLOCK_VIRTUAL, esp_efuse_timer_cb, s);
}

static Property esp_efuse_properties[] = {
    DEFINE_PROP_DRIVE("drive", ESPEfuseState, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESPEfuseClass* esp_efuse = ESP_EFUSE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp_efuse_reset_hold;
    dc->realize = esp_efuse_realize;
    device_class_set_props(dc, esp_efuse_properties);

    esp_efuse->get_spi_boot_crypt_cnt = virt_efuse_get_spi_boot_crypt_cnt;
    esp_efuse->get_dis_download_man_encrypt = virt_efuse_dis_download_manual_encrypt;
    esp_efuse->get_key_purpose = virt_efuse_get_key_purpose;
    esp_efuse->get_key = virt_efuse_get_key;
}

static const TypeInfo esp_efuse_info = {
    .name = TYPE_ESP_EFUSE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPEfuseState),
    .instance_init = esp_efuse_init,
    .class_init = esp_efuse_class_init,
    .class_size = sizeof(ESPEfuseClass),
    /* Do not allow any target to instanciate this class, it must instanciate a derived class */
    .abstract = true
};

static void esp_efuse_register_types(void)
{
    type_register_static(&esp_efuse_info);
}

type_init(esp_efuse_register_types)

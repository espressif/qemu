/*
 * ESP32-C3 eFuse emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
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
#include "hw/nvram/esp32c3_efuse.h"


#define EFUSE_DEBUG    0


#define EFUSE_DEFAULT_FILENAME "qemu_efuses.bin"

/**
 * @brief Specify the delay, in us of a a write or read operation, this will only be used to simulate
 * the delay the real efuses actually take on real hardware.
 */
#define EFUSE_OPERATION_DELAY_US    1000


static uint64_t esp32c3_efuse_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3EfuseState *s = ESP32C3_EFUSE(opaque);
    uint8_t  *content_8  = ((uint8_t*) &s->efuses) + addr;
    uint16_t *content_16 = (uint16_t*) content_8;
    uint32_t *content_32 = (uint32_t*) content_8;

    assert(addr < sizeof(s->efuses));

    if (size == 1) {
        return *content_8;
    } else if (size == 2) {
        return *content_16;
    } else {
        return *content_32;
    }
}

/**
 * @brief Return the start offset in the ESP32C3EfuseRegs structure of the given block.
 */
static int esp32c3_offset_of_block(int block_num)
{
    const int offsets[] = {
        ESP32C3_EFUSE_BLOCK0_ADDR,
        ESP32C3_EFUSE_BLOCK1_ADDR,
        ESP32C3_EFUSE_BLOCK2_ADDR,
        ESP32C3_EFUSE_BLOCK3_ADDR,
        ESP32C3_EFUSE_BLOCK4_ADDR,
        ESP32C3_EFUSE_BLOCK5_ADDR,
        ESP32C3_EFUSE_BLOCK6_ADDR,
        ESP32C3_EFUSE_BLOCK7_ADDR,
        ESP32C3_EFUSE_BLOCK8_ADDR,
        ESP32C3_EFUSE_BLOCK9_ADDR,
        ESP32C3_EFUSE_BLOCK10_ADDR,
    };

    assert(block_num < sizeof(offsets)/sizeof(int));
    return offsets[block_num];
}


/**
 * @brief Hide the protected efuses by overwriting them with 0s, this function shall be called after
 * calling `esp32c3_efuse_read`.
 */
static void esp32c3_hide_protected_block(ESP32C3EfuseState *s)
{
    uint32_t rd_protection = s->efuses.rd_repeat_data0.rd_dis;
    /* Only the BLOCK4 and upwards can be protected */
    int block_num = 4;

    while (rd_protection != 0) {
        if (rd_protection & 1) {
            /* Get the offset of the block in the ESP32C3EfuseRegs structure */
            const int offset = esp32c3_offset_of_block(block_num);
            uint8_t* from = ((uint8_t*) &s->efuses) + offset;
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
static void esp32c3_efuse_reload_from_blk(ESP32C3EfuseState *s)
{
    /* In theory, there are 4096 bits of efuses, in practice, the memory space allocated for
    * efuses stops at &rd_repeat_err0. */
    const uint32_t size = offsetof(ESP32C3EfuseRegs, rd_repeat_err0) - offsetof(ESP32C3EfuseRegs, rd_wr_dis);

    /* Load the efuses from the block device file (or mirror) */
    if (s->blk) {

        /* Load the file content inside the structure, starting at efuse rd_wr_dis */
        const int ret = blk_pread(s->blk, 0, size, &s->efuses.rd_wr_dis, 0);
        if (ret < 0) {
            error_report("%s: failed to read the block device (%d)", __func__, ret);
        }

    } else {

        assert(s->mirror);
        memcpy(&s->efuses.rd_wr_dis, s->mirror, size);

    }
}

/**
 * @brief Get a mask of the protected bits for BLOCK0.
 * A bit set to 1 marks a protected bit whereas a 0 marks an unprotected bit.
 *
 * @param wr_dis Write-disable register
 * @param block0_mask Mask containing exactly ESP32C3_EFUSE_BLOCK0_WORDS words that will
 *                    be filled with the masks described above.
 */
static void esp32c3_efuse_get_block0_protected_mask(uint32_t wr_dis, uint32_t *block0_mask)
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

    memset(block0_mask, 0, ESP32C3_EFUSE_BLOCK0_WORDS * sizeof(uint32_t));

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
static bool esp32c3_efuse_write_to_blk(ESP32C3EfuseState *s, const int block)
{
    const int size =  esp32c3_efuse_block_size(block);
    bool protected = false;
    /* Mask of protected bit for each word of a BLOCK, only used when writing BLOCK0 */
    uint32_t block_mask[ESP32C3_EFUSE_PGM_DATA_COUNT] = { 0 };

    /* If the block to protect is not BLOCK0 the check is rather simple */
    if (block != 0) {
        assert(block <= 10);
        /* BLOCK1 protection is bit 20, BLOCK2 protection is bit 21, etc... */
        const int offset = 19 + block;
        /* If the bit is 1, protection is enabled, we cannot write */
        protected = (s->efuses.rd_wr_dis >> offset) & 1;
    } else {
        /* BLOCK0 protection is done on a bit granularity, so for each word that composes it
         * get mask where 1 represents a protected bit, and 0 represents an unprotected bit */
        esp32c3_efuse_get_block0_protected_mask(s->efuses.rd_wr_dis, block_mask);
    }

    if (!protected) {
        /* Get the offset of the block in the ESP32C3EfuseRegs structure!
         * Subtract the offset of the BLOCK0 to get the offset of our block in the
         * binary file (blk).
         * The offset in struct must be in 32-bit words */
        const uint32_t offset_in_struct = esp32c3_offset_of_block(block) / sizeof(uint32_t);
        /* Offset in file must be in bytes */
        const uint32_t offset_in_file = esp32c3_offset_of_block(block) - esp32c3_offset_of_block(0);

        /* Generate the actual data to write to the file. Indeed, the programmed bits (1) shall
         * NOT be programmed to 0 as on real hardware an efuse cannot be reverted.
         * To do so, OR the content to burn with the existing content so that the 1s are never erased. */
        uint32_t *efuses = (uint32_t*) &s->efuses;
        uint32_t real_data[ESP32C3_EFUSE_PGM_DATA_COUNT];

        for (int i = 0; i < ESP32C3_EFUSE_PGM_DATA_COUNT; i++) {
            /* Offset of pgm_data is 0, let's use efuses[i] to retrieve the data.
             * efuses[i] represents the new value, efuses[offset_in_struct + i] represents the old value,
             * block_mask represents the protection, with 1 marking a bit as protected.
             * As such, the final result of an efuse value is:
             * Y = old_value | (~block_mask & new_value) */
            real_data[i] = efuses[offset_in_struct + i] | (~block_mask[i] & efuses[i]);
        }

        /* Write the new block data to the file (or RAM) */
        if (s->blk) {

            const int ret = blk_pwrite(s->blk, offset_in_file, size, real_data, 0);
            if (ret < 0) {
                error_report("%s: failed to write efuses to the block device (%d)", __func__, ret);
            }

        } else {

            assert(s->mirror);
            memcpy(s->mirror + offset_in_file, real_data, size);

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
static void esp32c3_efuse_timer_cb(void *opaque)
{
    ESP32C3EfuseState *s = ESP32C3_EFUSE(opaque);

    /* To make sure the command register did not change between the moment the operation was scheduled
     * and the moment the callback was triggered, restore its value to its original one. (mirror)
     * In any case, it will be set to 0 at the end of this function */
    s->efuses.cmd.val = s->op_cmd_mirror;

    /* No need to check the opcode again */
    if (s->efuses.cmd.read_cmd) {
        esp32c3_efuse_reload_from_blk(s);
        esp32c3_hide_protected_block(s);
        s->efuses.int_raw.read_done = 1;
    } else {
        assert(s->efuses.cmd.pgm_cmd);
        s->efuses.int_raw.pgm_done = 1;
    }

    /* In any case, reset the command register to show that the operation is finished */
    s->efuses.cmd.val = 0;

    /* Set the interrupt bits, if any is 1, trigger an interrupt */
    s->efuses.int_st.val = s->efuses.int_ena.val | s->efuses.int_raw.val;
    if (s->efuses.int_st.val) {
        qemu_irq_raise(s->irq);
    }
}


/**
 * @brief Start ESP32C3EfuseState's timer to simulate the efuse operation delay
 */
static void esp32c3_efuse_op_timer_start(ESP32C3EfuseState *s)
{
    const uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const uint64_t interval_ns = EFUSE_OPERATION_DELAY_US * 1000;
    timer_mod_anticipate_ns(&s->op_timer, ns_now + interval_ns);
}


static void esp32c3_efuse_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32C3EfuseState *s = ESP32C3_EFUSE(opaque);
    /* Address of the register to write if we consider efuses as an array of bytes */
    uint8_t  *content_8  = ((uint8_t*) &s->efuses) + addr;
    uint16_t *content_16 = (uint16_t*) content_8;
    uint32_t *content_32 = (uint32_t*) content_8;

#if EFUSE_DEBUG
    info_report("[EFUSE] Writing to 0x%08lx = 0x%08lx (size: %d)\n", addr, value, size);
#endif

    /* Check if the programming cmd block is being written */
    if (addr == offsetof(ESP32C3EfuseRegs, cmd)) {

        s->efuses.cmd.val = (uint32_t) value;

        /* The command bit and the opcode will be check by the underlying function */
        if (esp32c3_efuse_is_read_cmd(s)) {

            /* Save the register value and schedule a timer to simulate real efuse loading delay,
             * the copy will be done in the callback. */
            s->op_cmd_mirror = (uint32_t) value;
            esp32c3_efuse_op_timer_start(s);

        } else if (esp32c3_efuse_is_write_cmd(s)) {

            const bool success = esp32c3_efuse_write_to_blk(s, s->efuses.cmd.blk_num);

            if (success) {
                /* Same as for the read, schedule the timer and perform the actual transfer once it elapsed */
                s->op_cmd_mirror = (uint32_t) value;
                esp32c3_efuse_op_timer_start(s);
            } else {
                s->efuses.cmd.val = 0;
                s->op_cmd_mirror = 0;
            }
        } else {
            s->efuses.cmd.val = 0;
        }

        return;
    }

    /* The first registers, up to rd_wr_dis excluded, can be written to freely */
    if (addr < offsetof(ESP32C3EfuseRegs, rd_wr_dis))
    {
        if (size == 1) {
            *content_8 = value & 0xff;
        } else if (size == 2) {
            *content_16 = value & 0xffff;
        } else {
            *content_32 = value;
        }

        return;
    }

    /* Treat the interrupt-related cases separately */
    switch (addr) {
        case offsetof(ESP32C3EfuseRegs, int_clr):
            /* Only clear the bits that are set to 1 in the value */
            s->efuses.int_raw.val &= ((~value) & 0b11);
            break;
        case offsetof(ESP32C3EfuseRegs, int_raw):
            s->efuses.int_raw.val = 0;
            break;
        case offsetof(ESP32C3EfuseRegs, int_ena):
            s->efuses.int_ena.val = value & 0b11;
            break;

        /* For debugging purposes */
        case offsetof(ESP32C3EfuseRegs, dbg_erase_all):
            {
#if EFUSE_DEBUG
                info_report("[EFUSE] erasing all efuses!\n");
#endif
                uint32_t size = offsetof(ESP32C3EfuseRegs, rd_repeat_err0) - offsetof(ESP32C3EfuseRegs, rd_wr_dis);
                memset(&s->efuses.rd_wr_dis, 0, size);

                if (s->blk) {
                    int ret = blk_pwrite(s->blk, 0, size, &s->efuses.rd_wr_dis, 0);
                    if (ret != 0) {
                        error_report("ERROR WRITING FILE: %d\n", ret);
                        exit(1);
                    }
                } else {

                    assert(s->mirror);
                    memset(s->mirror, 0, ESP32C3_EFUSE_BYTE_COUNT);
                }
            }
            return;

        case offsetof(ESP32C3EfuseRegs, clk):
        case offsetof(ESP32C3EfuseRegs, conf):
        case offsetof(ESP32C3EfuseRegs, dac_conf):
        case offsetof(ESP32C3EfuseRegs, rd_tim_conf):
        case offsetof(ESP32C3EfuseRegs, wr_tim_conf1):
        case offsetof(ESP32C3EfuseRegs, wr_tim_conf2):
            /* Make sure we write these registers with a 32-bit access */
            assert(size >= 4);
            *content_32 = value;
            /* Fall-through */

        /* The other registers are read-only */
        default:
            return;
    }

    /* Check if any interrupt needs to be triggered or, on the contrary, lowered */
    const uint32_t new_status = (s->efuses.int_ena.val | s->efuses.int_raw.val) & 0b11;
    s->efuses.int_st.val = new_status;

    qemu_set_irq(s->irq, new_status ? 1 : 0);
}


static const MemoryRegionOps esp32c3_efuse_ops = {
    .read =  esp32c3_efuse_read,
    .write = esp32c3_efuse_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp32c3_efuse_reset(DeviceState *dev)
{
    ESP32C3EfuseState *s = ESP32C3_EFUSE(dev);
    timer_del(&s->op_timer);
    qemu_irq_lower(s->irq);
    esp32c3_efuse_reload_from_blk(s);
    esp32c3_hide_protected_block(s);
}

static void esp32c3_efuse_realize(DeviceState *dev, Error **errp)
{
    ESP32C3EfuseState *s = ESP32C3_EFUSE(dev);
    const char* error_msg = NULL;

    /* If no file was given as efuses, create a temporary one (in RAM). */
    if (s->blk == NULL) {
        s->mirror = g_malloc(ESP32C3_EFUSE_BYTE_COUNT);
        if (s->mirror == NULL) {
            error_msg = "failed to allocate memory for efuses";
            goto error;
        }

        memset(s->mirror, 0, ESP32C3_EFUSE_BYTE_COUNT);

        /* Set the chip revision to v0.3 and write it to the file */
        s->efuses.rd_mac_spi_sys_3.wafer_version_minor_low = 3;

        /* No need to rewrite the all the efuses, rd_mac_spi_sys_3 is enough */
        const uint32_t offset = offsetof(ESP32C3EfuseRegs, rd_mac_spi_sys_3) - esp32c3_offset_of_block(0);
        *((uint32_t*) (s->mirror + offset)) = s->efuses.rd_mac_spi_sys_3.val;

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

        esp32c3_efuse_reset((DeviceState*) s);
    }

    return;
error:
    error_setg(errp, "%s: %s", __func__, error_msg);
}

static void esp32c3_efuse_init(Object *obj)
{
    ESP32C3EfuseState *s = ESP32C3_EFUSE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_efuse_ops, s,
                          TYPE_ESP32C3_EFUSE, ESP32C3_EFUSE_IO_RANGE_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->op_timer, QEMU_CLOCK_VIRTUAL, esp32c3_efuse_timer_cb, s);
}

static Property esp32c3_efuse_properties[] = {
    DEFINE_PROP_DRIVE("drive", ESP32C3EfuseState, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c3_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_efuse_reset;
    dc->realize = esp32c3_efuse_realize;
    device_class_set_props(dc, esp32c3_efuse_properties);
}

static const TypeInfo esp32c3_efuse_info = {
    .name = TYPE_ESP32C3_EFUSE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3EfuseState),
    .instance_init = esp32c3_efuse_init,
    .class_init = esp32c3_efuse_class_init
};

static void esp32c3_efuse_register_types(void)
{
    type_register_static(&esp32c3_efuse_info);
}

type_init(esp32c3_efuse_register_types)

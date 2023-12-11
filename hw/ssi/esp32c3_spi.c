/*
 * ESP32 SPI controller
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "hw/ssi/esp32c3_spi.h"
#include "qemu/error-report.h"

#define SPI1_DEBUG      0
#define SPI1_WARNING    0


enum {
    CMD_RES = 0xab,
    CMD_DP = 0xb9,
    CMD_CE = 0x60,
    CMD_BE = 0xd8,
    CMD_SE = 0x20,
    CMD_PP = 0x02,
    CMD_WRSR = 0x1,
    CMD_RDSR = 0x5,
    CMD_RDID = 0x9f,
    CMD_WRDI = 0x4,
    CMD_WREN = 0x6,
    CMD_READ = 0x03,
    CMD_HPM = 0xa3,
};


typedef struct ESP32C3SpiTransaction {
    uint32_t cmd;
    uint32_t cmd_bytes;

    uint32_t addr;
    uint32_t addr_bytes;

    uint32_t dummy_bytes;

    void* data;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
} ESP32C3SpiTransaction;


static uint64_t esp32c3_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3SpiState *s = ESP32C3_SPI(opaque);

    uint64_t r = 0;
    switch (addr) {
        case A_SPI_MEM_CMD:
            r = 0;
            break;
        case A_SPI_MEM_ADDR:
            r = s->mem_addr;
            break;
        case A_SPI_MEM_CTRL:
            r = s->mem_ctrl;
            break;
        case A_SPI_MEM_CTRL1:
            r = s->mem_ctrl1;
            break;
        case A_SPI_MEM_CTRL2:
            r = s->mem_ctrl2;
            break;
        case A_SPI_MEM_CLOCK:
            r = s->mem_clock;
            break;
        case A_SPI_MEM_USER:
            r = s->mem_user;
            break;
        case A_SPI_MEM_USER1:
            r = s->mem_user1;
            break;
        case A_SPI_MEM_USER2:
            r = s->mem_user2;
            break;
        case A_SPI_MEM_MISO_DLEN:
            r = s->mem_miso_len;
            break;
        case A_SPI_MEM_MOSI_DLEN:
            r = s->mem_mosi_len;
            break;
        case A_SPI_MEM_RD_STATUS:
            r = s->mem_rd_st;
            break;
        case A_SPI_MEM_W0...A_SPI_MEM_W15:
            r = s->data_reg[(addr - A_SPI_MEM_W0) / sizeof(uint32_t)];
            break;
        case A_SPI_MEM_SUS_STATUS:
            r = s->mem_sus_st;
            break;
        default:
#if SPI1_WARNING
            warn_report("[SPI1] Unsupported read to 0x%lx", addr);
#endif
            break;
    }

#if SPI1_DEBUG
    info_report("[SPI1] Reading 0x%lx (0x%lx)", addr, r);
#endif

    return r;
}


static void esp32c3_spi_txrx_buffer(ESP32C3SpiState *s,
                                    const void *tx, int tx_bytes,
                                    void *rx, int rx_bytes)
{
    int bytes = MAX(tx_bytes, rx_bytes);
    for (int i = 0; i < bytes; ++i) {
        uint8_t byte = 0;
        if (byte < tx_bytes) {
            memcpy(&byte, tx + i, 1);
        }
        uint32_t res = ssi_transfer(s->spi, byte);
        if (byte < rx_bytes) {
            memcpy(rx + i, &res, 1);
        }
    }
}

static void esp32c3_spi_dummy_cycles(ESP32C3SpiState *s, uint32_t dummy_bytes) {
    for (int i = 0; i < dummy_bytes; i++) {
        ssi_transfer(s->spi, 0);
    }
}

static void esp32c3_spi_perform_transaction(ESP32C3SpiState *s, const ESP32C3SpiTransaction *t)
{
    qemu_set_irq(s->cs_gpio[0], 0);
    esp32c3_spi_txrx_buffer(s, &t->cmd, t->cmd_bytes, NULL, 0);
    esp32c3_spi_txrx_buffer(s, &t->addr, t->addr_bytes, NULL, 0);
    esp32c3_spi_dummy_cycles(s, t->dummy_bytes);
    esp32c3_spi_txrx_buffer(s, t->data, t->tx_bytes, t->data, t->rx_bytes);
    qemu_set_irq(s->cs_gpio[0], 1);
}


static inline void esp32c3_spi_get_addr(ESP32C3SpiState *s, uint32_t* addr, uint32_t* len)
{
    const uint32_t address = FIELD_EX32(s->mem_addr, SPI_MEM_ADDR, USR_ADDR_VALUE);
    /* SPI Flash expects the address to be sent with MSB first. We make the assumption that
     * the host computer uses a little-endian CPU. */
    *addr = bswap32(address);

    const uint32_t address_len = FIELD_EX32(s->mem_user1, SPI_MEM_USER1, USR_ADDR_BITLEN);
    *len = (address_len + 1) / 8;
}

static inline void esp32c3_spi_get_dummy(ESP32C3SpiState *s, uint32_t* len)
{
    const uint32_t dummy_count = FIELD_EX32(s->mem_user1, SPI_MEM_USER1, USR_DUMMY_CYCLELEN);

    /* Dummy cycles are interpreted as bytes by the emulated SPI Flash. As such, we shall convert
     * our dummy cycles count in bytes, rounding it up. For example:
     * 0 cycles = 0 byte
     * 1 cycle = 1 byte
     * ...
     * 8 cycles = 1 byte
     * 9 cycles = 2 bytes
     * etc..
     */
    *len = (dummy_count + 7) / 8;
}

static void esp32c3_spi_begin_transaction(ESP32C3SpiState *s)
{
    ESP32C3SpiTransaction t = {
        .data = s->data_reg
     };

    /* Get the number of bytes to read from the device */
    if (s->mem_user & R_SPI_MEM_USER_USR_MISO_MASK) {
        t.rx_bytes = FIELD_EX32(s->mem_miso_len, SPI_MEM_MISO_DLEN, USR_MISO_DBITLEN);
        t.rx_bytes = (t.rx_bytes + 1) / 8;
    }

    /* and the number of bytes to write to the device */
    if (s->mem_user & R_SPI_MEM_USER_USR_MOSI_MASK) {
        t.tx_bytes = FIELD_EX32(s->mem_mosi_len, SPI_MEM_MOSI_DLEN, USR_MOSI_DBITLEN);
        t.tx_bytes = (t.tx_bytes + 1) / 8;
    }

    /* Get the command and its length, in bytes
     * In theory we should test mem_user's command bit. In practice, if we do, `esptool`
     * cannot write flash successfully and detects an error */
    t.cmd = FIELD_EX32(s->mem_user2, SPI_MEM_USER2, USR_COMMAND_VALUE);
    t.cmd_bytes = FIELD_EX32(s->mem_user2, SPI_MEM_USER2, USR_COMMAND_BITLEN);
    t.cmd_bytes = (t.cmd_bytes + 1) / 8;

    /* Get the address and its length, in bytes */
    if (s->mem_user & R_SPI_MEM_USER_USR_ADDR_MASK) {
        esp32c3_spi_get_addr(s, &t.addr, &t.addr_bytes);
        if (t.addr_bytes > 0 && t.addr_bytes <= 4) {
            t.addr = t.addr >> (32 - t.addr_bytes * 8);
        }

        /* Only calculate and include dummy cycles when the USR_DUMMY bit is set! */
        if (s->mem_user & R_SPI_MEM_USER_USR_DUMMY_MASK) {
            esp32c3_spi_get_dummy(s, &t.dummy_bytes);
        }
    }

    esp32c3_spi_perform_transaction(s, &t);
}


static void esp32c3_spi_special_command(ESP32C3SpiState *s, uint32_t command)
{
    ESP32C3SpiTransaction t= {
        .cmd_bytes = 1
    };

    switch (command >> 19 << 19) {
        case R_SPI_MEM_CMD_FLASH_READ_MASK:
            t.cmd = CMD_READ;
            esp32c3_spi_get_addr(s, &t.addr, &t.addr_bytes);
            t.addr = t.addr >> (32 - t.addr_bytes * 8);
            t.data = s->data_reg;
            t.rx_bytes = (FIELD_EX32(s->mem_miso_len, SPI_MEM_MISO_DLEN, USR_MISO_DBITLEN) + 1) / 8;
            break;

        case R_SPI_MEM_CMD_FLASH_WREN_MASK:
            t.cmd = CMD_WREN;
            break;

        case R_SPI_MEM_CMD_FLASH_WRDI_MASK:
            t.cmd = CMD_WRDI;
            break;

        case R_SPI_MEM_CMD_FLASH_RDID_MASK:
            t.cmd = CMD_RDID;
            t.data = s->data_reg;
            t.rx_bytes = 3;
            break;

        case R_SPI_MEM_CMD_FLASH_RDSR_MASK:
            t.cmd = CMD_RDSR;
            t.data = &s->mem_rd_st;
            t.rx_bytes = 1;
            break;

        case R_SPI_MEM_CMD_FLASH_WRSR_MASK:
            t.cmd = CMD_WRSR;
            t.data = &s->mem_rd_st;
            t.tx_bytes = 1;
            break;

        case R_SPI_MEM_CMD_FLASH_PP_MASK:
            t.cmd = CMD_PP;
            t.data = s->data_reg;
            esp32c3_spi_get_addr(s, &t.addr, &t.addr_bytes);
            /* The number of bytes to process is in the upper-byte of address */
            t.tx_bytes = (s->mem_addr >> 24) & 0xff;
            /**
             * Page program expects a 24-bit page address, if the one written in mem_addr was
             * 0xNN_33_00_02 (where is "do not care"), after calling `esp32c3_spi_get_addr`, the
             * address becomes 0x02_00_33_NN. Thus, if we cast it to a byte array, arr[0] would give
             * `NN`, instead of `33`. We need to adjust the value in address.
             */
            t.addr = t.addr >> 8;
            break;

        case R_SPI_MEM_CMD_FLASH_SE_MASK:
            t.cmd = CMD_SE;
            esp32c3_spi_get_addr(s, &t.addr, &t.addr_bytes);
            /* For the same reasons as explained above, we need to adjust `t.addr`, but here, the shift
             * to perform is not fixed and depends on the address length */
            t.addr = t.addr >> (32 - t.addr_bytes * 8);
            break;

        case R_SPI_MEM_CMD_FLASH_BE_MASK:
            t.cmd = CMD_BE;
            esp32c3_spi_get_addr(s, &t.addr, &t.addr_bytes);
            t.addr = t.addr >> (32 - t.addr_bytes * 8);
            break;

        case R_SPI_MEM_CMD_FLASH_CE_MASK:
            t.cmd = CMD_CE;
            break;

        case R_SPI_MEM_CMD_FLASH_DP_MASK:
            t.cmd = CMD_DP;
            break;

        case R_SPI_MEM_CMD_FLASH_RES_MASK:
            t.cmd = CMD_RES;
            t.data = s->data_reg;
            t.rx_bytes = 3;
            break;

        case R_SPI_MEM_CMD_FLASH_HPM_MASK:
            t.cmd = CMD_HPM;
            /* HPM needs 24 dummy cycles, so sent 3 random bytes */
            t.data = s->data_reg;
            t.rx_bytes = 3;
            break;

        default:
#if SPI1_WARNING
            warn_report("[SPI1] Unsupported special command %x", command);
#endif
            return;
    }
    esp32c3_spi_perform_transaction(s, &t);
}


static void esp32c3_spi_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32C3SpiState *s = ESP32C3_SPI(opaque);
    uint32_t wvalue = (uint32_t) value;

#if SPI1_DEBUG
    info_report("[SPI1] Writing 0x%lx = %08lx", addr, value);
#endif

    switch (addr) {
        case A_SPI_MEM_CMD:
            if(wvalue & R_SPI_MEM_CMD_USR_MASK) {
                esp32c3_spi_begin_transaction(s);
            } else {
                esp32c3_spi_special_command(s, wvalue);
            }
            break;
        case A_SPI_MEM_ADDR:
            s->mem_addr = wvalue;
            break;
        case A_SPI_MEM_CTRL:
            s->mem_ctrl = wvalue;
            break;
        case A_SPI_MEM_CTRL1:
            s->mem_ctrl1 = wvalue;
            break;
        case A_SPI_MEM_CTRL2:
            s->mem_ctrl2 = wvalue;
            break;
        case A_SPI_MEM_CLOCK:
            s->mem_clock = wvalue;
            break;
        case A_SPI_MEM_USER:
            s->mem_user = wvalue;
            break;
        case A_SPI_MEM_USER1:
            s->mem_user1 = wvalue;
            break;
        case A_SPI_MEM_USER2:
            s->mem_user2 = wvalue;
            break;
        case A_SPI_MEM_MISO_DLEN:
            s->mem_miso_len = wvalue;
            break;
        case A_SPI_MEM_MOSI_DLEN:
            s->mem_mosi_len = wvalue;
            break;
        case A_SPI_MEM_RD_STATUS:
            s->mem_rd_st = wvalue;
            break;
        case A_SPI_MEM_W0...A_SPI_MEM_W15:
            s->data_reg[(addr - A_SPI_MEM_W0) / sizeof(uint32_t)] = wvalue;
            break;
        case A_SPI_MEM_SUS_STATUS:
            s->mem_sus_st = wvalue;
            break;
        default:
#if SPI1_WARNING
            warn_report("[SPI1] Unsupported write to 0x%lx (%08lx)", addr, value);
#endif
            break;
    }

}

/* Convert one of the hardware "bitlen" registers to a byte count */
static inline int bitlen_to_bytes(uint32_t val)
{
    return (val + 1 + 7) / 8; /* bitlen registers hold number of bits, minus one */
}

static const MemoryRegionOps esp32c3_spi_ops = {
    .read =  esp32c3_spi_read,
    .write = esp32c3_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_spi_reset(DeviceState *dev)
{
    ESP32C3SpiState *s = ESP32C3_SPI(dev);
    memset(s->data_reg, 0, ESP32C3_SPI_BUF_WORDS * sizeof(uint32_t));
    s->mem_ctrl1 = FIELD_DP32(s->mem_ctrl1, SPI_MEM_CTRL1, CS_HOLD_DLY_RES, 0x3ff);
    s->mem_clock = FIELD_DP32(s->mem_clock, SPI_MEM_CLOCK, CLKCNT_N, 3);
    s->mem_clock = FIELD_DP32(s->mem_clock, SPI_MEM_CLOCK, CLKCNT_H, 1);
    s->mem_clock = FIELD_DP32(s->mem_clock, SPI_MEM_CLOCK, CLKCNT_L, 3);

    s->mem_user = FIELD_DP32(s->mem_user, SPI_MEM_USER, USR_COMMAND, 1);

    s->mem_user1 = FIELD_DP32(s->mem_user1, SPI_MEM_USER1, USR_ADDR_BITLEN, 23);
    s->mem_user1 = FIELD_DP32(s->mem_user1, SPI_MEM_USER1, USR_DUMMY_CYCLELEN, 7);

    s->mem_user2 = FIELD_DP32(s->mem_user2, SPI_MEM_USER2, USR_COMMAND_BITLEN, 7);

    /* In case more registers are supported in the future (MEM_MISC, MEM_TX_CRC, ...)
     * update this function with their default values */
}

static void esp32c3_spi_realize(DeviceState *dev, Error **errp)
{
}

static void esp32c3_spi_init(Object *obj)
{
    ESP32C3SpiState *s = ESP32C3_SPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_spi_ops, s,
                          TYPE_ESP32C3_SPI, ESP32C3_SPI_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    // sysbus_init_irq(sbd, &s->irq);

    esp32c3_spi_reset(DEVICE(s));

    s->spi = ssi_create_bus(DEVICE(s), "spi");
    qdev_init_gpio_out_named(DEVICE(s), &s->cs_gpio[0], SSI_GPIO_CS, ESP32C3_SPI_CS_COUNT);
}

static Property esp32c3_spi_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c3_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_spi_reset;
    dc->realize = esp32c3_spi_realize;
    device_class_set_props(dc, esp32c3_spi_properties);
}

static const TypeInfo esp32c3_spi_info = {
    .name = TYPE_ESP32C3_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3SpiState),
    .instance_init = esp32c3_spi_init,
    .class_init = esp32c3_spi_class_init
};

static void esp32c3_spi_register_types(void)
{
    type_register_static(&esp32c3_spi_info);
}

type_init(esp32c3_spi_register_types)

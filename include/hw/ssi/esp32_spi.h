#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/ssi/ssi.h"

#define TYPE_ESP32_SPI "ssi.esp32.spi"
#define ESP32_SPI(obj) OBJECT_CHECK(Esp32SpiState, (obj), TYPE_ESP32_SPI)

#define ESP32_SPI_CS_COUNT      3
#define ESP32_SPI_BUF_WORDS     16

typedef struct Esp32SpiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq cs_gpio[ESP32_SPI_CS_COUNT];
    int num_cs;
    SSIBus *spi;

    uint32_t addr_reg;
    uint32_t ctrl_reg;
    uint32_t status_reg;
    uint32_t ctrl1_reg;
    uint32_t ctrl2_reg;
    uint32_t user_reg;
    uint32_t user1_reg;
    uint32_t user2_reg;
    uint32_t mosi_dlen_reg;
    uint32_t miso_dlen_reg;
    uint32_t pin_reg;
    uint32_t data_reg[ESP32_SPI_BUF_WORDS];
} Esp32SpiState;


REG32(SPI_CMD, 0x00)
    FIELD(SPI_CMD, READ, 31, 1)
    FIELD(SPI_CMD, WREN, 30, 1)
    FIELD(SPI_CMD, WRDI, 29, 1)
    FIELD(SPI_CMD, RDID, 28, 1)
    FIELD(SPI_CMD, RDSR, 27, 1)
    FIELD(SPI_CMD, WRSR, 26, 1)
    FIELD(SPI_CMD, PP, 25, 1)
    FIELD(SPI_CMD, SE, 24, 1)
    FIELD(SPI_CMD, BE, 23, 1)
    FIELD(SPI_CMD, CE, 22, 1)
    FIELD(SPI_CMD, DP, 21, 1)
    FIELD(SPI_CMD, RES, 20, 1)
    FIELD(SPI_CMD, HPM, 19, 1)
    FIELD(SPI_CMD, USR, 18, 1)
    FIELD(SPI_CMD, PES, 17, 1)
    FIELD(SPI_CMD, PER, 16, 1)

REG32(SPI_ADDR, 0x04)
REG32(SPI_CTRL, 0x08)
REG32(SPI_STATUS, 0x10)
    FIELD(SPI_STATUS, STATUS, 0, 16)

REG32(SPI_CTRL1, 0x0c)
REG32(SPI_CTRL2, 0x14)
REG32(SPI_USER, 0x1C)
    FIELD(SPI_USER, COMMAND, 31, 1)
    FIELD(SPI_USER, ADDR, 30, 1)
    FIELD(SPI_USER, DUMMY, 29, 1)
    FIELD(SPI_USER, MISO, 28, 1)
    FIELD(SPI_USER, MOSI, 27, 1)
    FIELD(SPI_USER, SIO, 16, 1)
    FIELD(SPI_USER, DOUTDIN, 0, 1)

REG32(SPI_USER1, 0x20)
    FIELD(SPI_USER1, ADDR_BITLEN, 26, 6)
    FIELD(SPI_USER1, DUMMY_CYCLELEN, 0, 8)

REG32(SPI_USER2, 0x24)
    FIELD(SPI_USER2, COMMAND_BITLEN, 28, 4)
    FIELD(SPI_USER2, COMMAND_VALUE, 0, 16)

REG32(SPI_MOSI_DLEN, 0x28)
REG32(SPI_MISO_DLEN, 0x2c)
REG32(SPI_PIN, 0x34)
REG32(SPI_SLAVE, 0x38)
    FIELD(SPI_SLAVE, TRANS_DONE, 4, 1)
    FIELD(SPI_SLAVE, TRANS_INTEN, 9, 1)
    FIELD(SPI_SLAVE, TRANS_CNT, 22, 4)
    FIELD(SPI_SLAVE, SLAVE_MODE, 30, 1)

REG32(SPI_W0, 0x80)
REG32(SPI_EXT0, 0xF0)
REG32(SPI_EXT1, 0xF4)
REG32(SPI_EXT2, 0xF8)
REG32(SPI_EXT3, 0xFC)



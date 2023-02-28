#pragma once

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/registerfields.h"

#include "chardev/char-fe.h"
#include "chardev/char-io.h"
#include "chardev/char-socket.h"

#define TYPE_ESP32_GPIO "esp32.gpio"
#define ESP32_GPIO(obj) OBJECT_CHECK(Esp32GpioState, (obj), TYPE_ESP32_GPIO)

REG32(GPIO_OUT_REG, 0x0004)
REG32(GPIO_OUT_W1TS_REG, 0x0008)
REG32(GPIO_OUT_W1TC_REG, 0x000c)

REG32(GPIO_OUT1_REG, 0x0010)
REG32(GPIO_OUT1_W1TS_REG, 0x0014)
REG32(GPIO_OUT1_W1TC_REG, 0x0018)

REG32(GPIO_ENABLE_REG, 0x0020)
REG32(GPIO_ENABLE_W1TS_REG, 0x0024)
REG32(GPIO_ENABLE_W1TC_REG, 0x0028)

REG32(GPIO_ENABLE1_REG, 0x002C)
REG32(GPIO_ENABLE1_W1TS_REG, 0x0030)
REG32(GPIO_ENABLE1_W1TC_REG, 0x0034)

REG32(GPIO_STRAP, 0x0038)

REG32(GPIO_IN_REG, 0x003c)
REG32(GPIO_IN1_REG, 0x0040)

#define ESP32_STRAP_MODE_FLASH_BOOT 0x12
#define ESP32_STRAP_MODE_UART_BOOT  0x0f

typedef struct Esp32GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t strap_mode;
    uint32_t gpio_in_reg;
    uint32_t gpio_in1_reg;

    uint32_t gpio_out_reg;
    uint32_t gpio_out1_reg;
    uint32_t gpio_enable_reg;
    uint32_t gpio_enable1_reg;

    char* chardev_name;
    Chardev *chardev;
    CharBackend charbe;
    QemuMutex mutex;
} Esp32GpioState;


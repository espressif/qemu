#pragma once

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/registerfields.h"

#define TYPE_ESP32_GPIO "esp32.gpio"
#define ESP32_GPIO(obj)             OBJECT_CHECK(Esp32GpioState, (obj), TYPE_ESP32_GPIO)
#define ESP32_GPIO_GET_CLASS(obj)   OBJECT_GET_CLASS(Esp32GpioClass, obj, TYPE_ESP32_GPIO)
#define ESP32_GPIO_CLASS(klass)     OBJECT_CLASS_CHECK(Esp32GpioClass, klass, TYPE_ESP32_GPIO)

REG32(GPIO_STRAP, 0x0038)

#define ESP32_STRAP_MODE_FLASH_BOOT 0x12
#define ESP32_STRAP_MODE_UART_BOOT  0x0f

#define ESP_GPIO_MAX_PINS 40

/* IO_MUX register bit definitions */
#define ESP_IOMUX_FUN_PD_BIT    7
#define ESP_IOMUX_FUN_PU_BIT    8

typedef struct Esp32GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion iomux;
    qemu_irq irq;
    uint32_t strap_mode;
    uint32_t strap_offset;

    /* GPIO output loopback state */
    uint32_t gpio_out;
    uint32_t gpio_enable;
    uint32_t gpio_pullup;   /* bitmask: pins with pull-up enabled */
    uint32_t out_off;       /* GPIO_OUT_REG offset */
    uint32_t out_w1ts_off;  /* GPIO_OUT_W1TS_REG offset */
    uint32_t out_w1tc_off;  /* GPIO_OUT_W1TC_REG offset */
    uint32_t enable_off;    /* GPIO_ENABLE_REG offset */
    uint32_t enable_w1ts_off;
    uint32_t enable_w1tc_off;
    uint32_t in_off;        /* GPIO_IN_REG offset */

    /* IO_MUX state */
    uint32_t iomux_regs[ESP_GPIO_MAX_PINS];
    uint32_t num_gpios;
} Esp32GpioState;

typedef struct Esp32GpioClass {
    SysBusDeviceClass parent_class;
} Esp32GpioClass;

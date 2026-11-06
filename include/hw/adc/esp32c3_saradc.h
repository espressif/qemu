/*
 * ESP32-C3 SAR ADC emulation (oneshot mode only)
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32C3_SARADC_H
#define ESP32C3_SARADC_H

#include "hw/sysbus.h"
#include "hw/registerfields.h"

#define TYPE_ESP32C3_SARADC "esp32c3.saradc"
#define ESP32C3_SARADC(obj) \
    OBJECT_CHECK(Esp32C3SarAdcState, (obj), TYPE_ESP32C3_SARADC)

#define ESP32C3_SARADC_MEM_SIZE     0x1000
/* The register block ends with APB_CTRL_DATE at 0x3fc */
#define ESP32C3_SARADC_REG_COUNT    (0x400 / sizeof(uint32_t))

/*
 * Pads with an ADC channel behind them: GPIO0..4 are ADC1 channels 0..4 and
 * GPIO5 is ADC2 channel 0.
 */
#define ESP32C3_SARADC_PAD_COUNT    6
/* Raw code a pad reads when the board says nothing about it: mid-scale */
#define ESP32C3_SARADC_RAW_DEFAULT  2048
/* Oneshot conversions are 12 bits wide */
#define ESP32C3_SARADC_RAW_MAX      4095

typedef struct Esp32C3SarAdcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ESP32C3_SARADC_REG_COUNT];

    /*
     * The raw 12-bit code each pad converts to, from the "pad<n>" properties.
     * Raw rather than millivolts: attenuation and the calibration curve are
     * not modelled, so this is exactly what adc_oneshot_read() hands back.
     */
    uint16_t pad_raw[ESP32C3_SARADC_PAD_COUNT];
} Esp32C3SarAdcState;

REG32(SARADC_ONETIME_SAMPLE, 0x20);
    FIELD(SARADC_ONETIME_SAMPLE, ONETIME_ATTEN, 23, 2);
    /* (unit << 3) | channel */
    FIELD(SARADC_ONETIME_SAMPLE, ONETIME_CHANNEL, 25, 4);
    FIELD(SARADC_ONETIME_SAMPLE, ONETIME_START, 29, 1);
    FIELD(SARADC_ONETIME_SAMPLE, ADC2_ONETIME_SAMPLE, 30, 1);
    FIELD(SARADC_ONETIME_SAMPLE, ADC1_ONETIME_SAMPLE, 31, 1);

REG32(SARADC_1_DATA_STATUS, 0x2c);
REG32(SARADC_2_DATA_STATUS, 0x30);

/* INT_ENA, INT_RAW, INT_ST and INT_CLR share one bit layout */
REG32(SARADC_INT_ENA, 0x40);
REG32(SARADC_INT_RAW, 0x44);
    FIELD(SARADC_INT_RAW, ADC2_DONE, 30, 1);
    FIELD(SARADC_INT_RAW, ADC1_DONE, 31, 1);
REG32(SARADC_INT_ST, 0x48);
REG32(SARADC_INT_CLR, 0x4c);

#endif /* ESP32C3_SARADC_H */

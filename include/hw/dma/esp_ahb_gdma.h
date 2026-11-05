/*
 * GDMA of the chips with the AHB_DMA register block (ESP32-C5, ESP32-C61)
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp_gdma.h"

#define TYPE_ESP_AHB_GDMA "esp.ahb.gdma"
#define ESP_AHB_GDMA(obj)           OBJECT_CHECK(ESPAhbGdmaState, (obj), TYPE_ESP_AHB_GDMA)
#define ESP_AHB_GDMA_GET_CLASS(obj) OBJECT_GET_CLASS(ESPAhbGdmaClass, obj, TYPE_ESP_AHB_GDMA)
#define ESP_AHB_GDMA_CLASS(klass)   OBJECT_CLASS_CHECK(ESPAhbGdmaClass, klass, TYPE_ESP_AHB_GDMA)

#define ESP_AHB_GDMA_MAX_CHANNELS   5

/* Offsets of the register block, which differs between chips */
typedef struct ESPAhbGdmaLayout {
    hwaddr out_int_start;
    hwaddr misc_conf;
    hwaddr date;
    hwaddr chan_start;
    /* Stride between channels, and offset of the OUT registers in a channel */
    hwaddr chan_size;
    hwaddr out_offset;
    /* Offsets in a direction's registers */
    hwaddr link_offset;
    hwaddr link_addr_offset;    /* 0 if the LINK_ADDR registers are global */
    /* Global LINK_ADDR registers, 0 if they are in the channel registers */
    hwaddr in_link_addr;
    hwaddr out_link_addr;
    hwaddr regs_size;
    /* Generic register for each word of a direction's registers */
    const DmaRegister *chan_regs;
    size_t chan_reg_count;
} ESPAhbGdmaLayout;

typedef struct ESPAhbGdmaState {
    ESPGdmaState parent;
    MemoryRegion iomem;
    /* Values of the IN_LINK_ADDR and OUT_LINK_ADDR registers */
    uint32_t link_addr[ESP_GDMA_CONF_COUNT][ESP_AHB_GDMA_MAX_CHANNELS];
} ESPAhbGdmaState;

typedef struct ESPAhbGdmaClass {
    ESPGdmaClass parent_class;
    ResettablePhases parent_phases;
    const ESPAhbGdmaLayout *layout;
} ESPAhbGdmaClass;

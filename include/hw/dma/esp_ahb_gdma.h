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

#define ESP_AHB_GDMA_MAX_CHANNELS   3

typedef struct ESPAhbGdmaState {
    ESPGdmaState parent;
    MemoryRegion iomem;
    /* Values of the IN_LINK_ADDR and OUT_LINK_ADDR registers */
    uint32_t link_addr[ESP_GDMA_CONF_COUNT][ESP_AHB_GDMA_MAX_CHANNELS];
} ESPAhbGdmaState;

typedef struct ESPAhbGdmaClass {
    ESPGdmaClass parent_class;
    ResettablePhases parent_phases;
} ESPAhbGdmaClass;

/*
 * ESP32-P4 AXI DMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp_gdma.h"

#define TYPE_ESP32P4_AXI_GDMA "esp32p4.axi_gdma"
#define ESP32P4_AXI_GDMA(obj) OBJECT_CHECK(ESP32P4AxiGdmaState, (obj), TYPE_ESP32P4_AXI_GDMA)
#define ESP32P4_AXI_GDMA_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ESP32P4AxiGdmaClass, obj, TYPE_ESP32P4_AXI_GDMA)
#define ESP32P4_AXI_GDMA_CLASS(klass) \
    OBJECT_CLASS_CHECK(ESP32P4AxiGdmaClass, klass, TYPE_ESP32P4_AXI_GDMA)

#define ESP32P4_AXI_GDMA_CHANNEL_COUNT 3

typedef struct ESP32P4AxiGdmaState {
    ESPGdmaState parent;
    MemoryRegion iomem;
    /* Values of the LINK2 registers, which hold the descriptor address */
    uint32_t link2[ESP_GDMA_CONF_COUNT][ESP32P4_AXI_GDMA_CHANNEL_COUNT];
    /* CONF0 as the guest wrote it. The generic model keeps a translated copy. */
    uint32_t conf0[ESP_GDMA_CONF_COUNT][ESP32P4_AXI_GDMA_CHANNEL_COUNT];
} ESP32P4AxiGdmaState;

typedef struct ESP32P4AxiGdmaClass {
    ESPGdmaClass parent_class;
    ResettablePhases parent_phases;
} ESP32P4AxiGdmaClass;

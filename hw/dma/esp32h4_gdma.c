/*
 * ESP32-H4 GDMA
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/dma/esp32h4_gdma.h"

static const DmaRegister esp32h4_gdma_chan_regs[] = {
    GDMA_CONF0_REG, GDMA_CONF1_REG, GDMA_FIFO_ST_REG, GDMA_POP_REG,
    GDMA_LINK_REG, GDMA_UNKNOWN /* LINK_ADDR */, GDMA_STATE_REG, GDMA_SUC_EOF_DESC_REG,
    GDMA_ERR_EOF_DESC_REG, GDMA_UNKNOWN /* DONE_DES_ADDR */, GDMA_DESC_ADDR_REG,
    GDMA_BF0_DESC_ADDR_REG, GDMA_BF1_DESC_ADDR_REG, GDMA_PRIORITY_REG, GDMA_PERI_SEL_REG,
};

static const ESPAhbGdmaLayout esp32h4_gdma_layout = {
    .out_int_start = 0x050,
    .misc_conf = 0x0A4,
    .date = 0x0A8,
    .chan_start = 0x100,
    .chan_size = 0x100,
    .out_offset = 0x080,
    .link_offset = 0x010,
    .link_addr_offset = 0x014,
    .regs_size = 0x628,
    .chan_regs = esp32h4_gdma_chan_regs,
    .chan_reg_count = ARRAY_SIZE(esp32h4_gdma_chan_regs),
};

static void esp32h4_gdma_class_init(ObjectClass *klass, void *data)
{
    ESP_GDMA_CLASS(klass)->m_channel_count = ESP32H4_GDMA_CHANNEL_COUNT;
    ESP_AHB_GDMA_CLASS(klass)->layout = &esp32h4_gdma_layout;
}

static const TypeInfo esp32h4_gdma_info = {
    .name = TYPE_ESP32H4_GDMA,
    .parent = TYPE_ESP_AHB_GDMA,
    .instance_size = sizeof(ESP32H4GdmaState),
    .class_init = esp32h4_gdma_class_init,
    .class_size = sizeof(ESP32H4GdmaClass),
};

static void esp32h4_gdma_register_types(void)
{
    type_register_static(&esp32h4_gdma_info);
}

type_init(esp32h4_gdma_register_types)

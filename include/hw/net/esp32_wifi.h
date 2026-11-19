/*
 * ESP32 WiFi MAC with a built-in access point
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_ESP32_WIFI_H
#define HW_NET_ESP32_WIFI_H

#include "hw/sysbus.h"
#include "net/net.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_ESP32_WIFI "esp32_wifi"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32WifiState, ESP32_WIFI)

#define ESP32_WIFI_REGS (0x1000 / 4)
#define ESP32_WIFI_MAC_RST_GPIO "mac-rst"

/*
 * MMIO regions: 0 is the MAC; the others are radio blocks the WiFi driver
 * calibrates through, which are not modelled (see esp32_wifi_rf_read).
 */
enum {
    ESP32_WIFI_MMIO_MAC,
    ESP32_WIFI_MMIO_FE2,
    ESP32_WIFI_MMIO_FE,
    ESP32_WIFI_MMIO_ANA,
    ESP32_WIFI_MMIO_NRX,
    ESP32_WIFI_MMIO_BB,
    ESP32_WIFI_MMIO_PHY,
    ESP32_WIFI_MMIO_PHYA,
    ESP32_WIFI_MMIO_WDEV,
    ESP32_WIFI_MMIO_BT,         /* radio coexistence */
    ESP32_WIFI_MMIO_COUNT,
};

typedef struct Esp32WifiRfBlock {
    MemoryRegion iomem;
    int index;
    uint32_t regs[ESP32_WIFI_REGS];
} Esp32WifiRfBlock;

typedef enum {
    ESP32_WIFI_STA_NONE,
    ESP32_WIFI_STA_AUTHENTICATED,
    ESP32_WIFI_STA_ASSOCIATED,
} Esp32WifiStaState;

struct Esp32WifiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Esp32WifiRfBlock rf[ESP32_WIFI_MMIO_COUNT];
    qemu_irq irq;
    NICConf conf;
    NICState *nic;

    uint32_t regs[ESP32_WIFI_REGS];
    uint32_t int_raw;
    uint32_t rx_head;           /* first RX descriptor (DMA_INLINK) */
    uint32_t rx_next;           /* next RX descriptor to fill */

    /* Frames waiting for an RX descriptor, oldest first. */
    GQueue rx_queue;
    QEMUTimer *rx_timer;
    QEMUTimer *beacon_timer;

    /* The access point and the one station it serves. */
    char *ssid;
    uint8_t channel;
    Esp32WifiStaState sta_state;
    uint8_t sta_mac[6];
    uint16_t seq;
};

#endif

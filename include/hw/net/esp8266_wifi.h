/*
 * ESP8266 WiFi MAC
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_ESP8266_WIFI_H
#define HW_NET_ESP8266_WIFI_H

#include "hw/net/esp_wifi_ap.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_ESP8266_WIFI "esp8266_wifi"
#define ESP8266_WIFI_MAC_RST_GPIO "mac-rst"
OBJECT_DECLARE_SIMPLE_TYPE(Esp8266WifiState, ESP8266_WIFI)

#define ESP8266_WIFI_BASE       0x3ff1fe00
#define ESP8266_WIFI_PAGE_SIZE  0x200
#define ESP8266_WIFI_PAGE_COUNT 17
#define ESP8266_WIFI_SIZE       (ESP8266_WIFI_PAGE_SIZE * \
                                 ESP8266_WIFI_PAGE_COUNT)

typedef struct Esp8266WifiPage {
    MemoryRegion iomem;
    struct Esp8266WifiState *wifi;
    hwaddr offset;
} Esp8266WifiPage;

struct Esp8266WifiState {
    SysBusDevice parent_obj;

    Esp8266WifiPage page[ESP8266_WIFI_PAGE_COUNT];
    qemu_irq irq;
    EspWifiApState ap;
    QEMUTimer *mac_timer;
    uint32_t regs[ESP8266_WIFI_SIZE / 4];
    uint32_t int_raw;
    uint32_t rx_next;
    uint32_t rx_done;           /* last filled descriptor, latched into 0x214 */
};

#endif

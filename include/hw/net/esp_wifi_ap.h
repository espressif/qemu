/*
 * Common 802.11 access point used by Espressif WiFi MAC models
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_ESP_WIFI_AP_H
#define HW_NET_ESP_WIFI_AP_H

#include "hw/qdev-core.h"
#include "net/net.h"
#include "qemu/queue.h"
#include "qemu/timer.h"

typedef enum {
    ESP_WIFI_STA_NONE,
    ESP_WIFI_STA_AUTHENTICATED,
    ESP_WIFI_STA_ASSOCIATED,
} EspWifiStaState;

typedef enum {
    ESP_WIFI_SOFTAP_NONE,
    ESP_WIFI_SOFTAP_AUTHENTICATING,
    ESP_WIFI_SOFTAP_ASSOCIATING,
    ESP_WIFI_SOFTAP_ASSOCIATED,
} EspWifiSoftApState;

typedef bool (*EspWifiDeliver)(void *opaque, const uint8_t *frame, size_t len);

typedef struct EspWifiApState {
    void *opaque;
    EspWifiDeliver deliver;
    NICConf conf;
    NICState *nic;

    GQueue rx_queue;
    QEMUTimer *rx_timer;
    QEMUTimer *beacon_timer;

    char *ssid;
    uint8_t channel;
    EspWifiStaState sta_state;
    uint8_t sta_mac[6];
    uint16_t seq;

    EspWifiSoftApState softap_state;
    uint8_t softap_bssid[6];
    char softap_ssid[33];
    uint8_t softap_station[6];
    uint8_t softap_peer[6];
    bool softap_peer_known;
    int64_t softap_last_beacon;
    int64_t softap_last_try;
    int64_t softap_last_null;
} EspWifiApState;

void esp_wifi_ap_init(EspWifiApState *s, void *opaque,
                      EspWifiDeliver deliver);
void esp_wifi_ap_finalize(EspWifiApState *s);
void esp_wifi_ap_realize(EspWifiApState *s, DeviceState *dev, Error **errp);
void esp_wifi_ap_reset(EspWifiApState *s);
void esp_wifi_ap_start(EspWifiApState *s);
void esp_wifi_ap_guest_tx(EspWifiApState *s, const uint8_t *frame, size_t len);

#endif

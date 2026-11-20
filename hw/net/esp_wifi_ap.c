/*
 * Common 802.11 access point for Espressif WiFi MAC models
 *
 * This is the chip-independent part of the ESP32 WiFi model, shared with the
 * ESP8266: QEMU's open access point, Ethernet bridging, and the station that
 * joins a guest SoftAP. Host frames enter the SoftAP with the station's MAC
 * address and ARP payloads rewritten to match, the same approach as the
 * PICSimLab fork (https://github.com/lcgamboa/qemu); this code is new.
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/net/esp_wifi_ap.h"
#include "hw/qdev-properties.h"
#include "net/net.h"
#include "trace.h"

#define FCS_LEN                 4
#define BEACON_INTERVAL_TU      100
#define BEACON_NS               (BEACON_INTERVAL_TU * 1024 * 1000LL)
#define RX_SPACING_NS           (100 * 1000)
#define RX_RETRY_NS             (1000 * 1000)
#define RX_QUEUE_MAX            64

#define FC_MGMT_ASSOC_REQ       0x00
#define FC_MGMT_ASSOC_RESP      0x10
#define FC_MGMT_PROBE_REQ       0x40
#define FC_MGMT_PROBE_RESP      0x50
#define FC_MGMT_BEACON          0x80
#define FC_MGMT_DISASSOC        0xa0
#define FC_MGMT_AUTH            0xb0
#define FC_MGMT_DEAUTH          0xc0
#define FC_DATA                 0x08
#define FC_TYPE_MASK            0x0c
#define FC_TYPE_DATA            0x08
#define FC1_TO_DS               0x01
#define FC1_FROM_DS             0x02
#define FC_NULL_DATA            0x48

#define HDR_LEN                 24
#define LLC_SNAP_LEN            8
#define ETH_HDR_LEN             14
#define FRAME_MAX               1600

#define CAP_ESS                 0x0001
#define CAP_PRIVACY             0x0010
#define CAP_SHORT_SLOT          0x0400
#define AID                     1

#define ETH_P_ARP_HI            0x08
#define ETH_P_ARP_LO            0x06
#define ARP_SHA                 (ETH_HDR_LEN + 8)
#define ARP_THA                 (ETH_HDR_LEN + 18)
#define ARP_LEN                 (ETH_HDR_LEN + 28)

#define SOFTAP_RETRY_NS         (500 * 1000 * 1000LL)
#define SOFTAP_GONE_NS          (3 * NANOSECONDS_PER_SECOND)
#define SOFTAP_NULL_NS          (10 * NANOSECONDS_PER_SECOND)

typedef struct EspWifiFrame {
    size_t len;
    uint8_t data[FRAME_MAX];
} EspWifiFrame;

static const uint8_t broadcast_mac[6] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static const uint8_t llc_snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
static const uint8_t rates[] = {
    0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
};
static const uint8_t ext_rates[] = { 0x30, 0x48, 0x60, 0x6c };

static const uint8_t *esp_wifi_ap_bssid(EspWifiApState *s)
{
    return s->conf.macaddr.a;
}

static bool esp_wifi_ap_addr_match(const uint8_t *addr, const uint8_t *own)
{
    return !memcmp(addr, own, 6) || !memcmp(addr, broadcast_mac, 6);
}

static void esp_wifi_ap_rx_timer(void *opaque)
{
    EspWifiApState *s = opaque;
    EspWifiFrame *f = g_queue_peek_head(&s->rx_queue);
    int64_t delay = RX_RETRY_NS;

    if (f && s->deliver(s->opaque, f->data, f->len)) {
        g_free(g_queue_pop_head(&s->rx_queue));
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
        delay = RX_SPACING_NS;
    }
    if (!g_queue_is_empty(&s->rx_queue)) {
        timer_mod(s->rx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay);
    }
}

static void esp_wifi_ap_queue(EspWifiApState *s, EspWifiFrame *f)
{
    if (g_queue_get_length(&s->rx_queue) >= RX_QUEUE_MAX) {
        trace_esp32_wifi_rx_drop(f->data[0], f->len);
        g_free(f);
        return;
    }
    g_queue_push_tail(&s->rx_queue, f);
    if (!timer_pending(s->rx_timer)) {
        timer_mod(s->rx_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + RX_SPACING_NS);
    }
}

static EspWifiFrame *esp_wifi_ap_new_frame(EspWifiApState *s, uint8_t fc0,
                                           uint8_t fc1, const uint8_t *a1,
                                           const uint8_t *a2,
                                           const uint8_t *a3)
{
    EspWifiFrame *f = g_new0(EspWifiFrame, 1);

    f->data[0] = fc0;
    f->data[1] = fc1;
    memcpy(f->data + 4, a1, 6);
    memcpy(f->data + 10, a2, 6);
    memcpy(f->data + 16, a3, 6);
    stw_le_p(f->data + 22, (s->seq++ & 0xfff) << 4);
    f->len = HDR_LEN;
    return f;
}

static void esp_wifi_ap_put(EspWifiFrame *f, const void *data, size_t len)
{
    memcpy(f->data + f->len, data, len);
    f->len += len;
}

static void esp_wifi_ap_put_ie(EspWifiFrame *f, uint8_t id, const void *data,
                               uint8_t len)
{
    f->data[f->len++] = id;
    f->data[f->len++] = len;
    esp_wifi_ap_put(f, data, len);
}

static void esp_wifi_ap_put_le16(EspWifiFrame *f, uint16_t v)
{
    stw_le_p(f->data + f->len, v);
    f->len += 2;
}

static void esp_wifi_ap_put_rates(EspWifiFrame *f)
{
    esp_wifi_ap_put_ie(f, 1, rates, sizeof(rates));
    esp_wifi_ap_put_ie(f, 50, ext_rates, sizeof(ext_rates));
}

static void esp_wifi_ap_send_bss(EspWifiApState *s, uint8_t fc0,
                                 const uint8_t *da)
{
    const uint8_t *bssid = esp_wifi_ap_bssid(s);
    EspWifiFrame *f = esp_wifi_ap_new_frame(s, fc0, 0, da, bssid, bssid);
    uint64_t tsf = cpu_to_le64(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL));
    static const uint8_t tim[] = { 0, 1, 0, 0 };

    esp_wifi_ap_put(f, &tsf, sizeof(tsf));
    esp_wifi_ap_put_le16(f, BEACON_INTERVAL_TU);
    esp_wifi_ap_put_le16(f, CAP_ESS | CAP_SHORT_SLOT);
    esp_wifi_ap_put_ie(f, 0, s->ssid, strlen(s->ssid));
    esp_wifi_ap_put_ie(f, 1, rates, sizeof(rates));
    esp_wifi_ap_put_ie(f, 3, &s->channel, 1);
    if (fc0 == FC_MGMT_BEACON) {
        esp_wifi_ap_put_ie(f, 5, tim, sizeof(tim));
    }
    esp_wifi_ap_put_ie(f, 50, ext_rates, sizeof(ext_rates));
    esp_wifi_ap_queue(s, f);
}

static void esp_wifi_ap_set_state(EspWifiApState *s, EspWifiStaState state)
{
    if (s->sta_state != state) {
        trace_esp32_wifi_sta_state(state);
        s->sta_state = state;
    }
}

static void esp_wifi_ap_mgmt(EspWifiApState *s, const uint8_t *frame,
                             size_t len)
{
    const uint8_t *sa = frame + 10;
    const uint8_t *bssid = esp_wifi_ap_bssid(s);
    const uint8_t *body = frame + HDR_LEN;
    EspWifiFrame *f;

    switch (frame[0]) {
    case FC_MGMT_PROBE_REQ:
        esp_wifi_ap_send_bss(s, FC_MGMT_PROBE_RESP, sa);
        break;
    case FC_MGMT_AUTH:
        if (len < HDR_LEN + 6 || lduw_le_p(body) != 0 ||
            lduw_le_p(body + 2) != 1) {
            break;
        }
        f = esp_wifi_ap_new_frame(s, FC_MGMT_AUTH, 0, sa, bssid, bssid);
        esp_wifi_ap_put_le16(f, 0);
        esp_wifi_ap_put_le16(f, 2);
        esp_wifi_ap_put_le16(f, 0);
        esp_wifi_ap_queue(s, f);
        memcpy(s->sta_mac, sa, 6);
        esp_wifi_ap_set_state(s, ESP_WIFI_STA_AUTHENTICATED);
        break;
    case FC_MGMT_ASSOC_REQ:
        if (s->sta_state == ESP_WIFI_STA_NONE || memcmp(sa, s->sta_mac, 6)) {
            break;
        }
        f = esp_wifi_ap_new_frame(s, FC_MGMT_ASSOC_RESP, 0, sa, bssid, bssid);
        esp_wifi_ap_put_le16(f, CAP_ESS | CAP_SHORT_SLOT);
        esp_wifi_ap_put_le16(f, 0);
        esp_wifi_ap_put_le16(f, 0xc000 | AID);
        esp_wifi_ap_put_rates(f);
        esp_wifi_ap_queue(s, f);
        esp_wifi_ap_set_state(s, ESP_WIFI_STA_ASSOCIATED);
        break;
    case FC_MGMT_DEAUTH:
    case FC_MGMT_DISASSOC:
        if (!memcmp(sa, s->sta_mac, 6)) {
            esp_wifi_ap_set_state(s, ESP_WIFI_STA_NONE);
        }
        break;
    }
}

static size_t esp_wifi_ap_to_eth(const uint8_t *frame, size_t len,
                                 const uint8_t *da, const uint8_t *sa,
                                 uint8_t *eth)
{
    size_t payload_len;

    if (frame[0] != FC_DATA || len < HDR_LEN + LLC_SNAP_LEN ||
        memcmp(frame + HDR_LEN, llc_snap, sizeof(llc_snap))) {
        return 0;
    }
    payload_len = len - HDR_LEN - LLC_SNAP_LEN;
    memcpy(eth, da, 6);
    memcpy(eth + 6, sa, 6);
    memcpy(eth + 12, frame + HDR_LEN + 6, 2);
    memcpy(eth + ETH_HDR_LEN, frame + HDR_LEN + LLC_SNAP_LEN, payload_len);
    return ETH_HDR_LEN + payload_len;
}

static void esp_wifi_ap_data(EspWifiApState *s, const uint8_t *frame,
                             size_t len)
{
    uint8_t eth[ETH_HDR_LEN + FRAME_MAX];
    size_t eth_len;

    if (s->sta_state != ESP_WIFI_STA_ASSOCIATED ||
        (frame[1] & 3) != FC1_TO_DS ||
        memcmp(frame + 4, esp_wifi_ap_bssid(s), 6)) {
        return;
    }
    eth_len = esp_wifi_ap_to_eth(frame, len, frame + 16, frame + 10, eth);
    if (eth_len) {
        qemu_send_packet(qemu_get_queue(s->nic), eth, eth_len);
    }
}

static void esp_wifi_ap_softap_set_state(EspWifiApState *s,
                                         EspWifiSoftApState state)
{
    if (s->softap_state != state) {
        trace_esp32_wifi_softap_state(state, s->softap_ssid);
        s->softap_state = state;
        s->softap_last_try = 0;
    }
}

static void esp_wifi_ap_softap_try(EspWifiApState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const uint8_t *ap = s->softap_bssid;
    EspWifiFrame *f;

    if (now - s->softap_last_try < SOFTAP_RETRY_NS) {
        return;
    }
    s->softap_last_try = now;
    if (s->softap_state == ESP_WIFI_SOFTAP_AUTHENTICATING) {
        f = esp_wifi_ap_new_frame(s, FC_MGMT_AUTH, 0, ap,
                                 s->softap_station, ap);
        esp_wifi_ap_put_le16(f, 0);
        esp_wifi_ap_put_le16(f, 1);
        esp_wifi_ap_put_le16(f, 0);
    } else if (s->softap_state == ESP_WIFI_SOFTAP_ASSOCIATING) {
        f = esp_wifi_ap_new_frame(s, FC_MGMT_ASSOC_REQ, 0, ap,
                                 s->softap_station, ap);
        esp_wifi_ap_put_le16(f, CAP_ESS | CAP_SHORT_SLOT);
        esp_wifi_ap_put_le16(f, 10);
        esp_wifi_ap_put_ie(f, 0, s->softap_ssid, strlen(s->softap_ssid));
        esp_wifi_ap_put_rates(f);
    } else {
        return;
    }
    esp_wifi_ap_queue(s, f);
}

static void esp_wifi_ap_softap_tick(EspWifiApState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (s->softap_state == ESP_WIFI_SOFTAP_NONE) {
        return;
    }
    if (now - s->softap_last_beacon > SOFTAP_GONE_NS) {
        esp_wifi_ap_softap_set_state(s, ESP_WIFI_SOFTAP_NONE);
    } else if (s->softap_state != ESP_WIFI_SOFTAP_ASSOCIATED) {
        esp_wifi_ap_softap_try(s);
    } else if (now - s->softap_last_null > SOFTAP_NULL_NS) {
        s->softap_last_null = now;
        esp_wifi_ap_queue(s, esp_wifi_ap_new_frame(s, FC_NULL_DATA, FC1_TO_DS,
                                                   s->softap_bssid,
                                                   s->softap_station,
                                                   s->softap_bssid));
    }
}

static void esp_wifi_ap_softap_beacon(EspWifiApState *s,
                                      const uint8_t *frame, size_t len)
{
    const uint8_t *body = frame + HDR_LEN;
    char ssid[33] = { 0 };

    if (len < HDR_LEN + 14 || body[12] != 0 || body[13] > 32 ||
        len < HDR_LEN + 14 + body[13] ||
        (lduw_le_p(body + 10) & CAP_PRIVACY)) {
        return;
    }
    memcpy(ssid, body + 14, body[13]);
    if (s->softap_state == ESP_WIFI_SOFTAP_NONE ||
        memcmp(s->softap_bssid, frame + 16, 6) ||
        strcmp(ssid, s->softap_ssid)) {
        memcpy(s->softap_bssid, frame + 16, 6);
        pstrcpy(s->softap_ssid, sizeof(s->softap_ssid), ssid);
        s->softap_state = ESP_WIFI_SOFTAP_NONE;
        esp_wifi_ap_softap_set_state(s, ESP_WIFI_SOFTAP_AUTHENTICATING);
    }
    s->softap_last_beacon = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    esp_wifi_ap_softap_try(s);
}

static void esp_wifi_ap_softap_frame(EspWifiApState *s,
                                     const uint8_t *frame, size_t len)
{
    const uint8_t *body = frame + HDR_LEN;
    uint8_t eth[ETH_HDR_LEN + FRAME_MAX];
    size_t eth_len;

    switch (frame[0]) {
    case FC_MGMT_AUTH:
        if (s->softap_state == ESP_WIFI_SOFTAP_AUTHENTICATING &&
            len >= HDR_LEN + 6 && lduw_le_p(body + 2) == 2 &&
            lduw_le_p(body + 4) == 0) {
            esp_wifi_ap_softap_set_state(s, ESP_WIFI_SOFTAP_ASSOCIATING);
            esp_wifi_ap_softap_try(s);
        }
        return;
    case FC_MGMT_ASSOC_RESP:
        if (s->softap_state == ESP_WIFI_SOFTAP_ASSOCIATING &&
            len >= HDR_LEN + 6 && lduw_le_p(body + 2) == 0) {
            esp_wifi_ap_softap_set_state(s, ESP_WIFI_SOFTAP_ASSOCIATED);
        }
        return;
    case FC_MGMT_DEAUTH:
    case FC_MGMT_DISASSOC:
        if (s->softap_state != ESP_WIFI_SOFTAP_NONE) {
            esp_wifi_ap_softap_set_state(s, ESP_WIFI_SOFTAP_AUTHENTICATING);
        }
        return;
    }
    if (s->softap_state != ESP_WIFI_SOFTAP_ASSOCIATED ||
        (frame[1] & 3) != FC1_FROM_DS) {
        return;
    }
    eth_len = esp_wifi_ap_to_eth(frame, len, frame + 4, frame + 16, eth);
    if (!eth_len) {
        return;
    }
    if (!memcmp(eth, s->softap_station, 6)) {
        if (!s->softap_peer_known) {
            return;
        }
        memcpy(eth, s->softap_peer, 6);
        if (eth[12] == ETH_P_ARP_HI && eth[13] == ETH_P_ARP_LO &&
            eth_len >= ARP_LEN &&
            !memcmp(eth + ARP_THA, s->softap_station, 6)) {
            memcpy(eth + ARP_THA, s->softap_peer, 6);
        }
    }
    qemu_send_packet(qemu_get_queue(s->nic), eth, eth_len);
}

static void esp_wifi_ap_softap_send(EspWifiApState *s, const uint8_t *buf,
                                    size_t size)
{
    EspWifiFrame *f;
    size_t body;

    memcpy(s->softap_peer, buf + 6, 6);
    s->softap_peer_known = true;
    f = esp_wifi_ap_new_frame(s, FC_DATA, FC1_TO_DS, s->softap_bssid,
                             s->softap_station, buf);
    esp_wifi_ap_put(f, llc_snap, sizeof(llc_snap));
    body = f->len;
    esp_wifi_ap_put(f, buf + 12, size - 12);
    if (buf[12] == ETH_P_ARP_HI && buf[13] == ETH_P_ARP_LO &&
        size >= ARP_LEN) {
        memcpy(f->data + body + ARP_SHA - 12, s->softap_station, 6);
    }
    esp_wifi_ap_queue(s, f);
}

void esp_wifi_ap_guest_tx(EspWifiApState *s, const uint8_t *frame, size_t len)
{
    trace_esp32_wifi_tx(len ? frame[0] : 0, len);
    if (len < HDR_LEN) {
        return;
    }
    if (frame[0] == FC_MGMT_BEACON) {
        esp_wifi_ap_softap_beacon(s, frame, len);
    } else if (!memcmp(frame + 4, s->softap_station, 6) ||
               ((frame[1] & 3) == FC1_FROM_DS &&
                !memcmp(frame + 10, s->softap_bssid, 6))) {
        esp_wifi_ap_softap_frame(s, frame, len);
    } else if ((frame[0] & FC_TYPE_MASK) == FC_TYPE_DATA) {
        esp_wifi_ap_data(s, frame, len);
    } else if (!(frame[0] & FC_TYPE_MASK) &&
               esp_wifi_ap_addr_match(frame + 4, esp_wifi_ap_bssid(s))) {
        esp_wifi_ap_mgmt(s, frame, len);
    }
}

static void esp_wifi_ap_beacon_timer(void *opaque)
{
    EspWifiApState *s = opaque;

    if (g_queue_is_empty(&s->rx_queue)) {
        esp_wifi_ap_send_bss(s, FC_MGMT_BEACON, broadcast_mac);
    }
    esp_wifi_ap_softap_tick(s);
    timer_mod(s->beacon_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + BEACON_NS);
}

static bool esp_wifi_ap_can_receive(NetClientState *nc)
{
    EspWifiApState *s = qemu_get_nic_opaque(nc);

    return g_queue_get_length(&s->rx_queue) < RX_QUEUE_MAX;
}

static ssize_t esp_wifi_ap_receive(NetClientState *nc, const uint8_t *buf,
                                   size_t size)
{
    EspWifiApState *s = qemu_get_nic_opaque(nc);
    bool group = buf[0] & 1;
    bool to_sta;
    EspWifiFrame *f;

    if (size < ETH_HDR_LEN ||
        size - ETH_HDR_LEN > FRAME_MAX - HDR_LEN - LLC_SNAP_LEN) {
        return size;
    }
    to_sta = s->sta_state == ESP_WIFI_STA_ASSOCIATED &&
             (group || !memcmp(buf, s->sta_mac, 6));
    if (to_sta) {
        f = esp_wifi_ap_new_frame(s, FC_DATA, FC1_FROM_DS, buf,
                                 esp_wifi_ap_bssid(s), buf + 6);
        esp_wifi_ap_put(f, llc_snap, sizeof(llc_snap));
        esp_wifi_ap_put(f, buf + 12, size - 12);
        esp_wifi_ap_queue(s, f);
    }
    if (s->softap_state == ESP_WIFI_SOFTAP_ASSOCIATED && (group || !to_sta)) {
        esp_wifi_ap_softap_send(s, buf, size);
    }
    return size;
}

static NetClientInfo net_esp_wifi_ap_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = esp_wifi_ap_can_receive,
    .receive = esp_wifi_ap_receive,
};

void esp_wifi_ap_init(EspWifiApState *s, void *opaque,
                      EspWifiDeliver deliver)
{
    s->opaque = opaque;
    s->deliver = deliver;
    g_queue_init(&s->rx_queue);
    s->rx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, esp_wifi_ap_rx_timer, s);
    s->beacon_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   esp_wifi_ap_beacon_timer, s);
}

void esp_wifi_ap_finalize(EspWifiApState *s)
{
    g_queue_clear_full(&s->rx_queue, g_free);
    timer_free(s->rx_timer);
    timer_free(s->beacon_timer);
}

void esp_wifi_ap_realize(EspWifiApState *s, DeviceState *dev, Error **errp)
{
    if (!s->ssid) {
        s->ssid = g_strdup("QEMU");
    }
    if (strlen(s->ssid) > 32) {
        error_setg(errp, "%s: ssid longer than 32 characters",
                   object_get_typename(OBJECT(dev)));
        return;
    }
    if (s->channel < 1 || s->channel > 13) {
        error_setg(errp, "%s: channel must be 1 to 13",
                   object_get_typename(OBJECT(dev)));
        return;
    }
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    memcpy(s->softap_station, s->conf.macaddr.a, 6);
    s->softap_station[5] ^= 0x80;
    s->nic = qemu_new_nic(&net_esp_wifi_ap_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

void esp_wifi_ap_reset(EspWifiApState *s)
{
    g_queue_clear_full(&s->rx_queue, g_free);
    timer_del(s->rx_timer);
    s->sta_state = ESP_WIFI_STA_NONE;
    s->softap_state = ESP_WIFI_SOFTAP_NONE;
    s->softap_ssid[0] = 0;
    s->softap_peer_known = false;
    timer_del(s->beacon_timer);
}

void esp_wifi_ap_start(EspWifiApState *s)
{
    if (!timer_pending(s->beacon_timer)) {
        timer_mod(s->beacon_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + BEACON_NS);
    }
}

/*
 * ESP32 WiFi MAC with a built-in access point
 *
 * Lets unmodified firmware that uses Espressif's WiFi driver (ESP-IDF,
 * Arduino, Tasmota, ...) join a network. The MAC exchanges 802.11 frames
 * with the guest through its DMA descriptor lists; a small access point in
 * this file answers them (beacons, probe, authentication, association) and
 * bridges data frames to a QEMU network backend:
 *
 *   -nic user,model=esp32_wifi[,hostfwd=tcp::8080-:80]
 *   -global esp32_wifi.ssid=QEMU -global esp32_wifi.channel=1
 *
 * Sources. The MAC register offsets and their behaviour, the RX status
 * header layout, the interrupt bits and the values the radio blocks must
 * read back were learned from the ESP32 WiFi emulation by Martin Johnson
 * (https://github.com/a159x36/qemu, 2022), as maintained in Luis Claudio
 * Gamboa Lopes' PICSimLab fork (https://github.com/lcgamboa/qemu,
 * hw/misc/esp32_wifi.c, esp32_phya.c, esp32_ana.c, esp32_fe.c). That code
 * was read, not copied: everything here is a new implementation, and the
 * access point does not use the MIT-licensed Kolbitsch-derived access point
 * (esp32_wifi_ap.c, esp32_wlan_packet.c) of those forks.
 *
 * Limitations: one open (unencrypted) access point and one station, no
 * SoftAP mode in the guest, no power save, a fixed channel, no migration.
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/net/esp32_wifi.h"
#include "exec/address-spaces.h"
#include "net/net.h"
#include "trace.h"

/* MAC registers (offsets into the 0x3ff73000 block) */
#define A_WIFI_MAC_ADDR0        0x040   /* own address, 6 bytes */
#define A_WIFI_MAC_ADDR1        0x048
#define A_WIFI_DMA_IN_STATUS    0x084
#define A_WIFI_DMA_INLINK       0x088
#define A_WIFI_NEXT_RX_DSCR     0x08c
#define A_WIFI_LAST_RX_DSCR     0x090
#define A_WIFI_INT_STATUS       0xc48
#define A_WIFI_INT_CLR          0xc4c
#define A_WIFI_TXQ_COMPLETE     0xcc8
#define A_WIFI_DMA_OUTLINK      0xd20
#define A_WIFI_DMA_OUT_STATUS   0xd24

#define WIFI_DMA_OUTLINK_START  0xc0000000
#define WIFI_INT_TX_DONE        0x00000080
#define WIFI_INT_RX_DONE        0x01000024

/* lldesc_t, the DMA descriptor shared with the other ESP32 DMA engines */
#define DESC_SIZE(w)            extract32(w, 0, 12)
#define DESC_OWNER_DMA          BIT(31)
#define DESC_EOF                BIT(30)

/* RX status header the MAC writes in front of each frame (7 words) */
#define RX_CTRL_WORDS           7
#define RX_RSSI                 (-40)
#define RX_NOISE_FLOOR          (-97)
#define RX_RATE_11M             11

#define FCS_LEN                 4
#define BEACON_INTERVAL_TU      100
#define BEACON_NS               (BEACON_INTERVAL_TU * 1024 * 1000LL)
#define RX_SPACING_NS           (100 * 1000)
#define RX_RETRY_NS             (1000 * 1000)  /* no free RX descriptor */
#define RX_QUEUE_MAX            64

/* 802.11 */
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
#define FC_SUBTYPE_MASK         0xf0
#define FC1_TO_DS               0x01
#define FC1_FROM_DS             0x02

#define HDR_LEN                 24
#define LLC_SNAP_LEN            8
#define ETH_HDR_LEN             14
#define FRAME_MAX               1600

#define CAP_ESS                 0x0001
#define CAP_SHORT_SLOT          0x0400
#define AID                     1

static const uint8_t broadcast_mac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t llc_snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
/* 1, 2, 5.5, 11 (basic), 6, 9, 12, 18 Mbit/s; then 24, 36, 48, 54 */
static const uint8_t rates[] = {
    0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
};
static const uint8_t ext_rates[] = { 0x30, 0x48, 0x60, 0x6c };

typedef struct Esp32WifiFrame {
    size_t len;                 /* without FCS */
    uint8_t data[FRAME_MAX];
} Esp32WifiFrame;

static const uint8_t *esp32_wifi_bssid(Esp32WifiState *s)
{
    return s->conf.macaddr.a;
}

static void esp32_wifi_update_irq(Esp32WifiState *s)
{
    qemu_set_irq(s->irq, s->int_raw != 0);
}

/* RX: frames from the access point to the guest */

static bool esp32_wifi_addr_match(const uint8_t *addr, const uint8_t *own)
{
    return !memcmp(addr, own, 6) || !memcmp(addr, broadcast_mac, 6);
}

/* Fill the next RX descriptor; false when the guest has none ready. */
static bool esp32_wifi_deliver(Esp32WifiState *s, Esp32WifiFrame *f)
{
    uint32_t rx_ctrl[RX_CTRL_WORDS] = { 0 };
    uint32_t desc[3];
    uint32_t len = f->len + FCS_LEN;
    const uint8_t *own0 = (uint8_t *)&s->regs[A_WIFI_MAC_ADDR0 / 4];
    const uint8_t *own1 = (uint8_t *)&s->regs[A_WIFI_MAC_ADDR1 / 4];
    const uint8_t *da = f->data + 4, *bssid = f->data + 16;
    uint8_t fcs[FCS_LEN] = { 0 };

    if (!s->rx_next) {
        return false;
    }
    address_space_read(&address_space_memory, s->rx_next,
                       MEMTXATTRS_UNSPECIFIED, desc, sizeof(desc));
    for (int i = 0; i < 3; i++) {
        desc[i] = le32_to_cpu(desc[i]);
    }
    if (!(desc[0] & DESC_OWNER_DMA) || !desc[1] ||
        DESC_SIZE(desc[0]) < sizeof(rx_ctrl) + len) {
        trace_esp32_wifi_rx_no_desc(s->rx_next, desc[0]);
        return false;
    }

    rx_ctrl[0] = deposit32(0, 0, 8, (uint8_t)RX_RSSI);
    rx_ctrl[0] = deposit32(rx_ctrl[0], 8, 5, RX_RATE_11M);
    rx_ctrl[0] = deposit32(rx_ctrl[0], 16, 12, len);
    rx_ctrl[0] = deposit32(rx_ctrl[0], 28, 1, esp32_wifi_addr_match(da, own0));
    rx_ctrl[0] = deposit32(rx_ctrl[0], 29, 1, esp32_wifi_addr_match(da, own1));
    rx_ctrl[0] = deposit32(rx_ctrl[0], 30, 1,
                           esp32_wifi_addr_match(bssid, own0));
    rx_ctrl[0] = deposit32(rx_ctrl[0], 31, 1,
                           esp32_wifi_addr_match(bssid, own1));
    rx_ctrl[2] = deposit32(0, 0, 8, (uint8_t)RX_NOISE_FLOOR);
    rx_ctrl[2] = deposit32(rx_ctrl[2], 16, 4, s->channel);
    rx_ctrl[3] = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    rx_ctrl[6] = deposit32(len, 12, 12, len);
    for (int i = 0; i < RX_CTRL_WORDS; i++) {
        rx_ctrl[i] = cpu_to_le32(rx_ctrl[i]);
    }

    address_space_write(&address_space_memory, desc[1], MEMTXATTRS_UNSPECIFIED,
                        rx_ctrl, sizeof(rx_ctrl));
    address_space_write(&address_space_memory, desc[1] + sizeof(rx_ctrl),
                        MEMTXATTRS_UNSPECIFIED, f->data, f->len);
    address_space_write(&address_space_memory,
                        desc[1] + sizeof(rx_ctrl) + f->len,
                        MEMTXATTRS_UNSPECIFIED, fcs, sizeof(fcs));
    /* The driver reads length and EOF; it hands the descriptor back itself. */
    desc[0] = deposit32(desc[0], 12, 12, sizeof(rx_ctrl) + len) | DESC_EOF;
    desc[0] = cpu_to_le32(desc[0]);
    address_space_write(&address_space_memory, s->rx_next,
                        MEMTXATTRS_UNSPECIFIED, &desc[0], 4);

    trace_esp32_wifi_rx(f->data[0], f->len);
    s->rx_next = desc[2] ? desc[2] : s->rx_head;
    s->int_raw |= WIFI_INT_RX_DONE;
    esp32_wifi_update_irq(s);
    return true;
}

static void esp32_wifi_rx_timer(void *opaque)
{
    Esp32WifiState *s = opaque;
    Esp32WifiFrame *f = g_queue_peek_head(&s->rx_queue);
    int64_t delay = RX_RETRY_NS;

    if (f && esp32_wifi_deliver(s, f)) {
        g_free(g_queue_pop_head(&s->rx_queue));
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
        delay = RX_SPACING_NS;
    }
    if (!g_queue_is_empty(&s->rx_queue)) {
        timer_mod(s->rx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay);
    }
}

/* Queue a frame for the guest; delivered from a timer, never re-entrantly. */
static void esp32_wifi_queue(Esp32WifiState *s, Esp32WifiFrame *f)
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

static Esp32WifiFrame *esp32_wifi_new_frame(Esp32WifiState *s, uint8_t fc0,
                                            uint8_t fc1, const uint8_t *a1,
                                            const uint8_t *a2,
                                            const uint8_t *a3)
{
    Esp32WifiFrame *f = g_new0(Esp32WifiFrame, 1);

    f->data[0] = fc0;
    f->data[1] = fc1;
    memcpy(f->data + 4, a1, 6);
    memcpy(f->data + 10, a2, 6);
    memcpy(f->data + 16, a3, 6);
    stw_le_p(f->data + 22, (s->seq++ & 0xfff) << 4);
    f->len = HDR_LEN;
    return f;
}

static void esp32_wifi_put(Esp32WifiFrame *f, const void *data, size_t len)
{
    memcpy(f->data + f->len, data, len);
    f->len += len;
}

static void esp32_wifi_put_ie(Esp32WifiFrame *f, uint8_t id, const void *data,
                              uint8_t len)
{
    f->data[f->len++] = id;
    f->data[f->len++] = len;
    esp32_wifi_put(f, data, len);
}

static void esp32_wifi_put_le16(Esp32WifiFrame *f, uint16_t v)
{
    stw_le_p(f->data + f->len, v);
    f->len += 2;
}

static void esp32_wifi_put_rates(Esp32WifiFrame *f)
{
    esp32_wifi_put_ie(f, 1, rates, sizeof(rates));
    esp32_wifi_put_ie(f, 50, ext_rates, sizeof(ext_rates));
}

/* Beacon or probe response body */
static void esp32_wifi_send_bss(Esp32WifiState *s, uint8_t fc0,
                                const uint8_t *da)
{
    const uint8_t *bssid = esp32_wifi_bssid(s);
    Esp32WifiFrame *f = esp32_wifi_new_frame(s, fc0, 0, da, bssid, bssid);
    uint64_t tsf = cpu_to_le64(qemu_clock_get_us(QEMU_CLOCK_VIRTUAL));
    static const uint8_t tim[] = { 0, 1, 0, 0 };

    esp32_wifi_put(f, &tsf, sizeof(tsf));
    esp32_wifi_put_le16(f, BEACON_INTERVAL_TU);
    esp32_wifi_put_le16(f, CAP_ESS | CAP_SHORT_SLOT);
    esp32_wifi_put_ie(f, 0, s->ssid, strlen(s->ssid));
    esp32_wifi_put_ie(f, 1, rates, sizeof(rates));
    esp32_wifi_put_ie(f, 3, &s->channel, 1);
    if (fc0 == FC_MGMT_BEACON) {
        esp32_wifi_put_ie(f, 5, tim, sizeof(tim));
    }
    esp32_wifi_put_ie(f, 50, ext_rates, sizeof(ext_rates));
    esp32_wifi_queue(s, f);
}

static void esp32_wifi_beacon_timer(void *opaque)
{
    Esp32WifiState *s = opaque;

    /* A guest that does not take frames gets no beacons piled up. */
    if (g_queue_is_empty(&s->rx_queue)) {
        esp32_wifi_send_bss(s, FC_MGMT_BEACON, broadcast_mac);
    }
    timer_mod(s->beacon_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + BEACON_NS);
}

static void esp32_wifi_set_state(Esp32WifiState *s, Esp32WifiStaState state)
{
    if (s->sta_state != state) {
        trace_esp32_wifi_sta_state(state);
        s->sta_state = state;
    }
}

/* TX: frames from the guest to the access point */

static void esp32_wifi_mgmt(Esp32WifiState *s, const uint8_t *frame,
                            size_t len)
{
    const uint8_t *sa = frame + 10, *bssid = esp32_wifi_bssid(s);
    const uint8_t *body = frame + HDR_LEN;
    Esp32WifiFrame *f;

    switch (frame[0]) {
    case FC_MGMT_PROBE_REQ:
        esp32_wifi_send_bss(s, FC_MGMT_PROBE_RESP, sa);
        break;
    case FC_MGMT_AUTH:
        /* open system authentication, transaction 1 */
        if (len < HDR_LEN + 6 || lduw_le_p(body) != 0 ||
            lduw_le_p(body + 2) != 1) {
            break;
        }
        f = esp32_wifi_new_frame(s, FC_MGMT_AUTH, 0, sa, bssid, bssid);
        esp32_wifi_put_le16(f, 0);      /* open system */
        esp32_wifi_put_le16(f, 2);      /* transaction 2 */
        esp32_wifi_put_le16(f, 0);      /* success */
        esp32_wifi_queue(s, f);
        memcpy(s->sta_mac, sa, 6);
        esp32_wifi_set_state(s, ESP32_WIFI_STA_AUTHENTICATED);
        break;
    case FC_MGMT_ASSOC_REQ:
        if (s->sta_state == ESP32_WIFI_STA_NONE || memcmp(sa, s->sta_mac, 6)) {
            break;
        }
        f = esp32_wifi_new_frame(s, FC_MGMT_ASSOC_RESP, 0, sa, bssid, bssid);
        esp32_wifi_put_le16(f, CAP_ESS | CAP_SHORT_SLOT);
        esp32_wifi_put_le16(f, 0);      /* success */
        esp32_wifi_put_le16(f, 0xc000 | AID);
        esp32_wifi_put_rates(f);
        esp32_wifi_queue(s, f);
        esp32_wifi_set_state(s, ESP32_WIFI_STA_ASSOCIATED);
        break;
    case FC_MGMT_DEAUTH:
    case FC_MGMT_DISASSOC:
        if (!memcmp(sa, s->sta_mac, 6)) {
            esp32_wifi_set_state(s, ESP32_WIFI_STA_NONE);
        }
        break;
    }
}

/* To-DS data frame from the station: addr1 BSSID, addr2 SA, addr3 DA. */
static void esp32_wifi_data(Esp32WifiState *s, const uint8_t *frame,
                            size_t len)
{
    const uint8_t *payload = frame + HDR_LEN + LLC_SNAP_LEN;
    uint8_t eth[ETH_HDR_LEN + FRAME_MAX];
    size_t payload_len;

    if (s->sta_state != ESP32_WIFI_STA_ASSOCIATED ||
        frame[0] != FC_DATA || (frame[1] & 3) != FC1_TO_DS ||
        len < HDR_LEN + LLC_SNAP_LEN ||
        memcmp(frame + HDR_LEN, llc_snap, sizeof(llc_snap))) {
        return;     /* null data, QoS, or not ours */
    }
    payload_len = len - HDR_LEN - LLC_SNAP_LEN;
    memcpy(eth, frame + 16, 6);
    memcpy(eth + 6, frame + 10, 6);
    memcpy(eth + 12, frame + HDR_LEN + 6, 2);
    memcpy(eth + ETH_HDR_LEN, payload, payload_len);
    qemu_send_packet(qemu_get_queue(s->nic), eth, ETH_HDR_LEN + payload_len);
}

static void esp32_wifi_transmit(Esp32WifiState *s, uint32_t desc_addr)
{
    uint8_t frame[FRAME_MAX];
    size_t len = 0;

    /* Gather the descriptor chain up to EOF. */
    for (int n = 0; desc_addr && n < 16; n++) {
        uint32_t desc[3];
        uint32_t chunk;

        address_space_read(&address_space_memory, desc_addr,
                           MEMTXATTRS_UNSPECIFIED, desc, sizeof(desc));
        for (int i = 0; i < 3; i++) {
            desc[i] = le32_to_cpu(desc[i]);
        }
        chunk = MIN(extract32(desc[0], 12, 12), sizeof(frame) - len);
        address_space_read(&address_space_memory, desc[1],
                           MEMTXATTRS_UNSPECIFIED, frame + len, chunk);
        len += chunk;
        if (desc[0] & DESC_EOF) {
            break;
        }
        desc_addr = desc[2];
    }

    trace_esp32_wifi_tx(len ? frame[0] : 0, len);
    if (len < HDR_LEN + FCS_LEN) {
        return;
    }
    len -= FCS_LEN;     /* the driver's length counts the FCS */
    if ((frame[0] & FC_TYPE_MASK) == FC_TYPE_DATA) {
        esp32_wifi_data(s, frame, len);
    } else if (!(frame[0] & FC_TYPE_MASK) &&
               (esp32_wifi_addr_match(frame + 4, esp32_wifi_bssid(s)))) {
        esp32_wifi_mgmt(s, frame, len);
    }
}

/* NIC backend: Ethernet frames from the host network to the station */

static bool esp32_wifi_can_receive(NetClientState *nc)
{
    Esp32WifiState *s = qemu_get_nic_opaque(nc);

    return s->sta_state != ESP32_WIFI_STA_ASSOCIATED ||
           g_queue_get_length(&s->rx_queue) < RX_QUEUE_MAX;
}

static ssize_t esp32_wifi_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    Esp32WifiState *s = qemu_get_nic_opaque(nc);
    Esp32WifiFrame *f;

    if (s->sta_state != ESP32_WIFI_STA_ASSOCIATED || size < ETH_HDR_LEN ||
        size - ETH_HDR_LEN > FRAME_MAX - HDR_LEN - LLC_SNAP_LEN ||
        (memcmp(buf, s->sta_mac, 6) && !(buf[0] & 1))) {
        return size;    /* not for the station: dropped, as a radio would */
    }
    /* From-DS: addr1 DA, addr2 BSSID, addr3 SA. */
    f = esp32_wifi_new_frame(s, FC_DATA, FC1_FROM_DS, buf,
                             esp32_wifi_bssid(s), buf + 6);
    esp32_wifi_put(f, llc_snap, sizeof(llc_snap));
    esp32_wifi_put(f, buf + 12, 2);
    esp32_wifi_put(f, buf + ETH_HDR_LEN, size - ETH_HDR_LEN);
    esp32_wifi_queue(s, f);
    return size;
}

static NetClientInfo net_esp32_wifi_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = esp32_wifi_can_receive,
    .receive = esp32_wifi_receive,
};

/* Registers */

static uint64_t esp32_wifi_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp32WifiState *s = opaque;

    switch (addr) {
    case A_WIFI_DMA_IN_STATUS:
        return s->regs[addr / 4] & ~1;
    case A_WIFI_DMA_INLINK:
        return s->rx_head;
    case A_WIFI_INT_STATUS:
    case A_WIFI_INT_CLR:
        return s->int_raw;
    case A_WIFI_TXQ_COMPLETE:
    case A_WIFI_DMA_OUT_STATUS:
        return 1;   /* transmissions complete at once */
    default:
        return s->regs[addr / 4];
    }
}

static void esp32_wifi_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    Esp32WifiState *s = opaque;

    switch (addr) {
    case A_WIFI_DMA_INLINK:
        s->rx_head = s->rx_next = val;
        s->regs[A_WIFI_NEXT_RX_DSCR / 4] = val;
        s->regs[A_WIFI_LAST_RX_DSCR / 4] = val;
        /* The driver is up: the access point starts beaconing. */
        if (val && !timer_pending(s->beacon_timer)) {
            timer_mod(s->beacon_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + BEACON_NS);
        }
        break;
    case A_WIFI_INT_CLR:
        s->int_raw &= ~val;
        esp32_wifi_update_irq(s);
        return;
    case A_WIFI_DMA_OUTLINK:
        if (val & WIFI_DMA_OUTLINK_START) {
            /* The descriptor address is the low 20 bits of a DRAM address. */
            esp32_wifi_transmit(s, 0x3ff00000 | extract32(val, 0, 20));
            s->int_raw |= WIFI_INT_TX_DONE;
            esp32_wifi_update_irq(s);
        }
        break;
    }
    s->regs[addr / 4] = val;
}

static const MemoryRegionOps esp32_wifi_ops = {
    .read = esp32_wifi_read,
    .write = esp32_wifi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/*
 * Radio blocks. The driver programs and calibrates the RF front end, the
 * analog block and the PHY through these; QEMU has no radio, so they are
 * register files (or constants) that read back what the calibration loops
 * wait for. The values are the ones the reference emulation (see the top of
 * this file) found to work.
 */
static uint64_t esp32_wifi_rf_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp32WifiRfBlock *b = opaque;

    switch (b->index) {
    case ESP32_WIFI_MMIO_FE2:
    case ESP32_WIFI_MMIO_NRX:
    case ESP32_WIFI_MMIO_BB:
    case ESP32_WIFI_MMIO_PHY:
        return 0xffffffff;      /* every done/ready flag set */
    case ESP32_WIFI_MMIO_FE:
        if (addr == 0x7c || addr == 0x174) {
            return 0xffffffff;
        }
        break;
    case ESP32_WIFI_MMIO_ANA:
        switch (addr) {
        case 0x04:
            return 0xfdffffff;
        case 0x44:
        case 0x4c:
        case 0xc4:
            return 0xffffffff;
        }
        break;
    }
    return b->regs[addr / 4];
}

static void esp32_wifi_rf_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    Esp32WifiRfBlock *b = opaque;

    b->regs[addr / 4] = val;
}

static const MemoryRegionOps esp32_wifi_rf_ops = {
    .read = esp32_wifi_rf_read,
    .write = esp32_wifi_rf_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/* MAC reset: also what DPORT_CORE_RST_EN.WIFIMAC_RST does before a restart. */
static void esp32_wifi_mac_reset(Esp32WifiState *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    s->int_raw = 0;
    s->rx_head = s->rx_next = 0;
    g_queue_clear_full(&s->rx_queue, g_free);
    timer_del(s->rx_timer);
    s->sta_state = ESP32_WIFI_STA_NONE;
    timer_del(s->beacon_timer);
    esp32_wifi_update_irq(s);
}

static void esp32_wifi_mac_rst(void *opaque, int n, int level)
{
    if (level) {
        esp32_wifi_mac_reset(opaque);
    }
}

static void esp32_wifi_reset_hold(Object *obj, ResetType type)
{
    Esp32WifiState *s = ESP32_WIFI(obj);

    for (int i = 1; i < ESP32_WIFI_MMIO_COUNT; i++) {
        memset(s->rf[i].regs, 0, sizeof(s->rf[i].regs));
    }
    esp32_wifi_mac_reset(s);
}

static void esp32_wifi_realize(DeviceState *dev, Error **errp)
{
    Esp32WifiState *s = ESP32_WIFI(dev);

    if (!s->ssid) {
        s->ssid = g_strdup("QEMU");
    }
    if (strlen(s->ssid) > 32) {
        error_setg(errp, "esp32_wifi: ssid longer than 32 characters");
        return;
    }
    if (s->channel < 1 || s->channel > 13) {
        error_setg(errp, "esp32_wifi: channel must be 1 to 13");
        return;
    }
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_esp32_wifi_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void esp32_wifi_init(Object *obj)
{
    Esp32WifiState *s = ESP32_WIFI(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_wifi_ops, s,
                          TYPE_ESP32_WIFI, sizeof(s->regs));
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    for (int i = 1; i < ESP32_WIFI_MMIO_COUNT; i++) {
        static const char *const names[ESP32_WIFI_MMIO_COUNT] = {
            [ESP32_WIFI_MMIO_FE2] = "esp32_wifi.fe2",
            [ESP32_WIFI_MMIO_FE] = "esp32_wifi.fe",
            [ESP32_WIFI_MMIO_ANA] = "esp32_wifi.ana",
            [ESP32_WIFI_MMIO_NRX] = "esp32_wifi.nrx",
            [ESP32_WIFI_MMIO_BB] = "esp32_wifi.bb",
            [ESP32_WIFI_MMIO_PHY] = "esp32_wifi.phy",
            [ESP32_WIFI_MMIO_PHYA] = "esp32_wifi.phya",
            [ESP32_WIFI_MMIO_WDEV] = "esp32_wifi.wdev",
            [ESP32_WIFI_MMIO_BT] = "esp32_wifi.bt",
        };

        s->rf[i].index = i;
        memory_region_init_io(&s->rf[i].iomem, obj, &esp32_wifi_rf_ops,
                              &s->rf[i], names[i], sizeof(s->rf[i].regs));
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->rf[i].iomem);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), esp32_wifi_mac_rst,
                            ESP32_WIFI_MAC_RST_GPIO, 1);
    g_queue_init(&s->rx_queue);
    s->rx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, esp32_wifi_rx_timer, s);
    s->beacon_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   esp32_wifi_beacon_timer, s);
}

static void esp32_wifi_finalize(Object *obj)
{
    Esp32WifiState *s = ESP32_WIFI(obj);

    g_queue_clear_full(&s->rx_queue, g_free);
    timer_free(s->rx_timer);
    timer_free(s->beacon_timer);
}

static Property esp32_wifi_properties[] = {
    DEFINE_NIC_PROPERTIES(Esp32WifiState, conf),
    DEFINE_PROP_STRING("ssid", Esp32WifiState, ssid),
    DEFINE_PROP_UINT8("channel", Esp32WifiState, channel, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_wifi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "ESP32 WiFi MAC with a built-in access point";
    dc->realize = esp32_wifi_realize;
    rc->phases.hold = esp32_wifi_reset_hold;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    device_class_set_props(dc, esp32_wifi_properties);
}

static const TypeInfo esp32_wifi_info = {
    .name = TYPE_ESP32_WIFI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32WifiState),
    .instance_init = esp32_wifi_init,
    .instance_finalize = esp32_wifi_finalize,
    .class_init = esp32_wifi_class_init,
};

static void esp32_wifi_register_types(void)
{
    type_register_static(&esp32_wifi_info);
}

type_init(esp32_wifi_register_types)

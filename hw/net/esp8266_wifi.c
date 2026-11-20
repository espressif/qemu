/*
 * ESP8266 WiFi MAC with a built-in access point
 *
 * The register addresses, interrupt bits, descriptor states, and TX queue
 * layout were derived from traces and disassembly of Espressif's MIT-licensed
 * NONOS SDK 2.2.2 as linked into Tasmota.  The register region was
 * cross-checked against SuperHouse esp-open-rtos
 * (core/include/esp/wdev_regs.h, BSD-3-Clause).  The public 12-byte RX control
 * layout comes from Espressif SDK documentation.  This is a new
 * implementation; no source was copied from either project.
 *
 * Usage, as for esp32_wifi (station mode, and the guest's SoftAP portal):
 *   -nic user,model=esp8266_wifi[,hostfwd=tcp::8080-:80]
 *   -nic user,model=esp8266_wifi,net=192.168.4.0/24,host=192.168.4.2,
 *        dhcpstart=192.168.4.1,hostfwd=tcp::8080-192.168.4.1:80
 *
 * Limitations: open networks and one station on each side, no encryption,
 * no TBTT interrupt (guest SoftAP beaconing works without it), no power
 * save, no migration.
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/net/esp8266_wifi.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "qemu/timer.h"
#include "trace.h"

#define A_RX_HEAD               0x0208
#define A_RX_DONE               0x0214
#define A_INT_STATUS            0x0e20
#define A_INT_CLEAR             0x0e24
#define A_BSSID1               0x0e30
#define A_MAC_ADDR0             0x0e48
#define A_MAC_ADDR1             0x0e50
#define A_TX_OUTCOME            0x0e84
/*
 * WDEV system time (0x3ff20c00): the free-running 1 MHz counter behind the
 * SDK's system_get_time() and so Arduino's micros()/millis() (esp-open-rtos
 * wdev_regs.h); read-only.
 */
#define A_SYS_TIME              0x0e00
#define A_TSF_LOW               0x1248
#define A_TSF_HIGH              0x124c
#define A_MAC_TIMER_CTRL        0x12c8
#define A_MAC_TIMER_LOW         0x12cc
#define A_MAC_TIMER_HIGH        0x12d0
#define A_TXQ0_KICK             0x0fc4
#define TXQ_STRIDE              0x18
#define TXQ_COUNT               8

#define INT_RX_DONE             BIT(8)
#define INT_TX_START            BIT(18)
#define INT_TX_DONE             BIT(19)
#define INT_MAC_TIMER           BIT(26)
#define TX_START                0xc0000000

#define DESC_CAPACITY(w)        extract32(w, 0, 12)
#define DESC_OWNER_HW           BIT(31)
#define DESC_DONE               BIT(30)
#define RX_CTRL_SIZE            12
#define FCS_LEN                 4
#define FRAME_MAX               1600
#define RX_RSSI_DBM             (-40)
#define RX_RATE_11M             3

static void esp8266_wifi_update_irq(Esp8266WifiState *s)
{
    qemu_set_irq(s->irq, s->int_raw != 0);
}

static void esp8266_wifi_mac_timer(void *opaque)
{
    Esp8266WifiState *s = opaque;

    s->regs[A_MAC_TIMER_CTRL / 4] &= ~BIT(31);
    s->int_raw |= INT_MAC_TIMER;
    esp8266_wifi_update_irq(s);
}

static void esp8266_wifi_update_mac_timer(Esp8266WifiState *s)
{
    uint64_t deadline_us;

    if (!(s->regs[A_MAC_TIMER_CTRL / 4] & BIT(31))) {
        timer_del(s->mac_timer);
        return;
    }
    deadline_us = ((uint64_t)s->regs[A_MAC_TIMER_HIGH / 4] << 32) |
                  s->regs[A_MAC_TIMER_LOW / 4];
    timer_mod(s->mac_timer, deadline_us * 1000);
}

static bool esp8266_wifi_addr_match(const uint8_t *addr, const uint8_t *own)
{
    return !memcmp(addr, own, 6);
}

static bool esp8266_wifi_deliver(void *opaque, const uint8_t *frame,
                                 size_t frame_len)
{
    Esp8266WifiState *s = opaque;
    hwaddr desc_addr = s->rx_next;
    uint32_t desc[3];
    uint32_t rx_ctrl[RX_CTRL_SIZE / 4] = { 0 };
    uint32_t dma_len = RX_CTRL_SIZE + frame_len + FCS_LEN;
    const uint8_t *own0 = (uint8_t *)&s->regs[A_MAC_ADDR0 / 4];
    const uint8_t *own1 = (uint8_t *)&s->regs[A_MAC_ADDR1 / 4];
    const uint8_t *bssid1 = (uint8_t *)&s->regs[A_BSSID1 / 4];
    const uint8_t *da = frame + 4;
    const uint8_t *bssid;
    uint32_t fcs = 0;

    if (!s->rx_next || frame_len < 24 || dma_len > FRAME_MAX) {
        return false;
    }
    switch (frame[1] & 3) {
    case 1:
        bssid = frame + 4;
        break;
    case 2:
        bssid = frame + 10;
        break;
    default:
        bssid = frame + 16;
        break;
    }
    trace_esp32_wifi_rx(frame[0], frame_len);
    address_space_read(&address_space_memory, desc_addr,
                       MEMTXATTRS_UNSPECIFIED, desc, sizeof(desc));
    for (unsigned i = 0; i < ARRAY_SIZE(desc); i++) {
        desc[i] = le32_to_cpu(desc[i]);
    }
    if (!(desc[0] & DESC_OWNER_HW) || !desc[1] || !desc[2] ||
        DESC_CAPACITY(desc[0]) < dma_len) {
        return false;
    }

    rx_ctrl[0] = deposit32(0, 0, 8, 96 + RX_RSSI_DBM);
    rx_ctrl[0] = deposit32(rx_ctrl[0], 8, 4, RX_RATE_11M);
    rx_ctrl[0] = deposit32(rx_ctrl[0], 12, 1, da[0] & 1);
    rx_ctrl[0] = deposit32(rx_ctrl[0], 16, 12, frame_len + FCS_LEN);
    rx_ctrl[0] = deposit32(rx_ctrl[0], 28, 1,
                           esp8266_wifi_addr_match(da, own0));
    rx_ctrl[0] = deposit32(rx_ctrl[0], 29, 1,
                           esp8266_wifi_addr_match(da, own1));
    rx_ctrl[0] = deposit32(rx_ctrl[0], 30, 1,
                           !memcmp(bssid, s->ap.conf.macaddr.a, 6));
    rx_ctrl[0] = deposit32(rx_ctrl[0], 31, 1,
                           !memcmp(bssid, bssid1, 6));
    rx_ctrl[2] = deposit32(0, 16, 4, s->ap.channel);
    for (unsigned i = 0; i < ARRAY_SIZE(rx_ctrl); i++) {
        rx_ctrl[i] = cpu_to_le32(rx_ctrl[i]);
    }

    address_space_write(&address_space_memory, desc[1],
                        MEMTXATTRS_UNSPECIFIED, rx_ctrl, sizeof(rx_ctrl));
    address_space_write(&address_space_memory, desc[1] + RX_CTRL_SIZE,
                        MEMTXATTRS_UNSPECIFIED, frame, frame_len);
    address_space_write(&address_space_memory,
                        desc[1] + RX_CTRL_SIZE + frame_len,
                        MEMTXATTRS_UNSPECIFIED, &fcs, sizeof(fcs));

    desc[0] &= ~(DESC_OWNER_HW | DESC_DONE | (0xfff << 12));
    desc[0] |= DESC_DONE | (dma_len << 12);
    desc[0] = cpu_to_le32(desc[0]);
    address_space_write(&address_space_memory, desc_addr,
                        MEMTXATTRS_UNSPECIFIED, &desc[0], sizeof(desc[0]));
    s->rx_done = desc_addr;
    s->rx_next = desc[2];
    s->int_raw |= INT_RX_DONE;
    esp8266_wifi_update_irq(s);
    return true;
}

static void esp8266_wifi_transmit(Esp8266WifiState *s, hwaddr kick_addr,
                                  uint32_t val)
{
    uint8_t frame[FRAME_MAX];
    uint32_t desc[2];
    uint32_t len = s->regs[(kick_addr + 4) / 4] & 0xfff;
    hwaddr desc_addr = 0x3ffe0000 | extract32(val, 0, 17);

    if (!len || len > sizeof(frame)) {
        return;
    }
    address_space_read(&address_space_memory, desc_addr,
                       MEMTXATTRS_UNSPECIFIED, desc, sizeof(desc));
    desc[1] = le32_to_cpu(desc[1]);
    if (!desc[1]) {
        return;
    }
    address_space_read(&address_space_memory, desc[1],
                       MEMTXATTRS_UNSPECIFIED, frame, len);
    esp_wifi_ap_guest_tx(&s->ap, frame, len);

    /*
     * wDev_ProcessFiq takes the queue of a TX start/done from bits 12..15 and
     * the outcome from bits 28..31 (0: success) of this register.
     */
    s->regs[A_TX_OUTCOME / 4] =
        deposit32(0, 12, 4, (A_TXQ0_KICK - kick_addr) / TXQ_STRIDE);
    s->int_raw |= INT_TX_START | INT_TX_DONE;
    esp8266_wifi_update_irq(s);
}

static bool esp8266_wifi_is_tx_kick(hwaddr addr)
{
    for (unsigned q = 0; q < TXQ_COUNT; q++) {
        if (addr == A_TXQ0_KICK - q * TXQ_STRIDE) {
            return true;
        }
    }
    return false;
}

static uint64_t esp8266_wifi_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp8266WifiPage *page = opaque;
    Esp8266WifiState *s = page->wifi;
    hwaddr offset = page->offset + addr;

    uint64_t value;

    if (offset == A_SYS_TIME) {
        value = (uint32_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000);
    } else if (offset == A_TSF_LOW) {
        value = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    } else if (offset == A_TSF_HIGH) {
        value = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) >> 32;
    } else if (offset == A_INT_STATUS) {
        /*
         * The ISR reads the status, then the last filled descriptor. Frames
         * delivered in between must not show there yet: the ISR would
         * consume (and recycle) them, then re-enter on the still pending
         * bit with a descriptor it already gave back, and assert.
         */
        s->regs[A_RX_DONE / 4] = s->rx_done;
        value = s->int_raw;
    } else if (offset == A_INT_CLEAR) {
        value = s->int_raw;
    } else {
        value = ldn_le_p((uint8_t *)s->regs + offset, size);
    }
    trace_esp8266_wifi_read(offset, value, size);
    return value;
}

static void esp8266_wifi_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    Esp8266WifiPage *page = opaque;
    Esp8266WifiState *s = page->wifi;
    hwaddr offset = page->offset + addr;

    trace_esp8266_wifi_write(offset, val, size);
    if (offset == A_SYS_TIME) {
        return;
    }
    if (offset == A_INT_CLEAR && size == 4) {
        s->int_raw &= ~val;
        /* RX done stays pending for frames not yet latched into 0x214. */
        if (s->rx_done != s->regs[A_RX_DONE / 4]) {
            s->int_raw |= INT_RX_DONE;
        }
        esp8266_wifi_update_irq(s);
        return;
    }
    stn_le_p((uint8_t *)s->regs + offset, size, val);
    if (offset == A_RX_HEAD && size == 4) {
        s->rx_next = val;
        if (val) {
            esp_wifi_ap_start(&s->ap);
        }
    } else if (size == 4 && esp8266_wifi_is_tx_kick(offset) &&
               (val & TX_START) == TX_START) {
        esp8266_wifi_transmit(s, offset, val);
    } else if (offset == A_MAC_TIMER_CTRL && size == 4) {
        esp8266_wifi_update_mac_timer(s);
    }
}

static const MemoryRegionOps esp8266_wifi_ops = {
    .read = esp8266_wifi_read,
    .write = esp8266_wifi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void esp8266_wifi_reset_hold(Object *obj, ResetType type)
{
    Esp8266WifiState *s = ESP8266_WIFI(obj);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[A_MAC_ADDR0 / 4] = 0xcd34fe18;
    s->regs[A_MAC_ADDR0 / 4 + 1] = 0x000001ef;
    s->int_raw = 0;
    s->rx_next = 0;
    s->rx_done = 0;
    timer_del(s->mac_timer);
    esp_wifi_ap_reset(&s->ap);
    esp8266_wifi_update_irq(s);
}

static void esp8266_wifi_realize(DeviceState *dev, Error **errp)
{
    Esp8266WifiState *s = ESP8266_WIFI(dev);

    esp_wifi_ap_realize(&s->ap, dev, errp);
}

/* Raised when the SDK stops the WiFi block before a restart or deep sleep. */
static void esp8266_wifi_mac_rst(void *opaque, int n, int level)
{
    if (level) {
        esp8266_wifi_reset_hold(OBJECT(opaque), RESET_TYPE_COLD);
    }
}

static void esp8266_wifi_init(Object *obj)
{
    Esp8266WifiState *s = ESP8266_WIFI(obj);

    for (unsigned i = 0; i < ESP8266_WIFI_PAGE_COUNT; i++) {
        Esp8266WifiPage *page = &s->page[i];

        page->wifi = s;
        page->offset = i * ESP8266_WIFI_PAGE_SIZE;
        memory_region_init_io(&page->iomem, obj, &esp8266_wifi_ops, page,
                              TYPE_ESP8266_WIFI, ESP8266_WIFI_PAGE_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &page->iomem);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), esp8266_wifi_mac_rst,
                            ESP8266_WIFI_MAC_RST_GPIO, 1);
    esp_wifi_ap_init(&s->ap, s, esp8266_wifi_deliver);
    s->mac_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                esp8266_wifi_mac_timer, s);
}

static void esp8266_wifi_finalize(Object *obj)
{
    Esp8266WifiState *s = ESP8266_WIFI(obj);

    esp_wifi_ap_finalize(&s->ap);
    timer_free(s->mac_timer);
}

static Property esp8266_wifi_properties[] = {
    DEFINE_NIC_PROPERTIES(Esp8266WifiState, ap.conf),
    DEFINE_PROP_STRING("ssid", Esp8266WifiState, ap.ssid),
    DEFINE_PROP_UINT8("channel", Esp8266WifiState, ap.channel, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp8266_wifi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "ESP8266 WiFi MAC with a built-in access point";
    dc->realize = esp8266_wifi_realize;
    rc->phases.hold = esp8266_wifi_reset_hold;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    device_class_set_props(dc, esp8266_wifi_properties);
}

static const TypeInfo esp8266_wifi_info = {
    .name = TYPE_ESP8266_WIFI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266WifiState),
    .instance_init = esp8266_wifi_init,
    .instance_finalize = esp8266_wifi_finalize,
    .class_init = esp8266_wifi_class_init,
};

static void esp8266_wifi_register_types(void)
{
    type_register_static(&esp8266_wifi_info);
}

type_init(esp8266_wifi_register_types)

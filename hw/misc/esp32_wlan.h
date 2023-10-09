/**
 * QEMU WLAN device emulation
 *
 * Copyright (c) 2008 Clemens Kolbitsch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Modifications:
 *  2008-February-24  Clemens Kolbitsch :
 *                                  New implementation based on ne2000.c
 *  18/1/22 Martin Johnson : Modified for esp32 wifi emulation
 *
 */

#ifndef esp32_wlan_h
#define esp32_wlan_h 1


#define DEBUG_Esp32_WLAN

#ifdef DEBUG_Esp32_WLAN
#define DEBUG_PRINT_AP(x) \
    do { \
        struct timeval __tt; \
        gettimeofday(&__tt, NULL); \
        printf("%u:%u  ", (unsigned)__tt.tv_sec, (unsigned)__tt.tv_usec); \
        printf x ;\
    } while (0)
#else
#define DEBUG_PRINT_AP(x)
#endif


#define IEEE80211_IDLE                  0xff

#define IEEE80211_TYPE_MGT              0x00
#define IEEE80211_TYPE_CTL              0x01
#define IEEE80211_TYPE_DATA             0x02

#define IEEE80211_TYPE_MGT_SUBTYPE_BEACON           0x08
#define IEEE80211_TYPE_MGT_SUBTYPE_ACTION           0x0d
#define IEEE80211_TYPE_MGT_SUBTYPE_PROBE_REQ        0x04
#define IEEE80211_TYPE_MGT_SUBTYPE_PROBE_RESP       0x05
#define IEEE80211_TYPE_MGT_SUBTYPE_AUTHENTICATION   0x0b
#define IEEE80211_TYPE_MGT_SUBTYPE_DEAUTHENTICATION 0x0c
#define IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_REQ  0x00
#define IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_RESP 0x01
#define IEEE80211_TYPE_MGT_SUBTYPE_DISASSOCIATION   0x0a

#define IEEE80211_TYPE_CTL_SUBTYPE_ACK          0x0d

#define IEEE80211_TYPE_DATA_SUBTYPE_DATA        0x00

#define IEEE80211_BEACON_PARAM_SSID             0x00
#define IEEE80211_BEACON_PARAM_RATES            0x01
#define IEEE80211_BEACON_PARAM_CHANNEL          0x03
#define IEEE80211_BEACON_PARAM_EXTENDED_RATES   0x32
#define IEEE80211_BEACON_PARAM_TIM              0x05


#define IEEE80211_HEADER_SIZE               24

typedef struct beacon_info_t {
    uint64_t timestamp;
    uint16_t interval;
    uint16_t capability;
}  QEMU_PACKED beacon_info_t;

typedef uint8_t macaddr_t[6];

#define BROADCAST (uint8_t[]){0xff,0xff,0xff,0xff,0xff,0xff}

typedef struct mac80211_frame {
    struct mac80211_frame_control {
        unsigned    protocol_version    : 2;
        unsigned    type            : 2;
        unsigned    sub_type        : 4;
        unsigned    to_ds           : 1;
        unsigned    from_ds         : 1;
        unsigned    _flags:6;
    } QEMU_PACKED frame_control;
    uint16_t  duration_id;
    macaddr_t receiver_address;
    macaddr_t transmitter_address;
    macaddr_t address_3;
    struct mac80211_sequence_control {
        unsigned    fragment_number     : 4;
        unsigned    sequence_number     : 12;
    } QEMU_PACKED sequence_control;
    // variable length, 2312 byte plus 4 byte frame-checksum
    union {
        uint8_t     data_and_fcs[2316];
        beacon_info_t beacon_info;
    };
    unsigned int frame_length;
    int signal_strength;
    int pos;
    struct mac80211_frame *next_frame;
}  QEMU_PACKED mac80211_frame;

typedef struct access_point_info {
    const char *ssid;
    int channel;
    int sigstrength;
    macaddr_t mac_address;
} access_point_info;

enum esp32_ap_state {
    Esp32_WLAN__STATE_NOT_AUTHENTICATED,
    Esp32_WLAN__STATE_AUTHENTICATED,
    Esp32_WLAN__STATE_ASSOCIATED,
    Esp32_WLAN__STATE_STA_NOT_AUTHENTICATED,
    Esp32_WLAN__STATE_STA_AUTHENTICATED,
    Esp32_WLAN__STATE_STA_ASSOCIATED,
    Esp32_WLAN__STATE_STA_DHCP,
};

#define Esp32_WLAN__MAX_INJECT_QUEUE_SIZE 20

typedef struct {
    signed rssi:8;                /**< Received Signal Strength Indicator(RSSI) of packet. unit: dBm */
    unsigned rate:5;              /**< PHY rate encoding of the packet. Only valid for non HT(11bg) packet */
    unsigned :1;                  /**< reserved */
    unsigned sig_mode:2;          /**< 0: non HT(11bg) packet; 1: HT(11n) packet; 3: VHT(11ac) packet */
    unsigned legacy_length:12;    /**< copy of the length */
    unsigned damatch0:1;          /* destination matches address0 */
    unsigned damatch1:1;          /* destination matches address1 */
    unsigned bssidmatch0:1;       /* bssid matches address0  */
    unsigned bssidmatch1:1;       /* bssid matches address1  */
    unsigned mcs:7;               /**< Modulation Coding Scheme. If is HT(11n) packet, shows the modulation, range from 0 to 76(MSC0 ~ MCS76) */
    unsigned cwb:1;               /**< Channel Bandwidth of the packet. 0: 20MHz; 1: 40MHz */
    unsigned :16;                 /**< reserved */
    unsigned smoothing:1;         /**< reserved */
    unsigned not_sounding:1;      /**< reserved */
    unsigned :1;                  /**< reserved */
    unsigned aggregation:1;       /**< Aggregation. 0: MPDU packet; 1: AMPDU packet */
    unsigned stbc:2;              /**< Space Time Block Code(STBC). 0: non STBC packet; 1: STBC packet */
    unsigned fec_coding:1;        /**< Flag is set for 11n packets which are LDPC */
    unsigned sgi:1;               /**< Short Guide Interval(SGI). 0: Long GI; 1: Short GI */
    signed noise_floor:8;         /**< noise floor of Radio Frequency Module(RF). unit: 0.25dBm*/
    unsigned ampdu_cnt:8;         /**< ampdu cnt */
    unsigned channel:4;           /**< primary channel on which this packet is received */
    unsigned secondary_channel:4; /**< secondary channel on which this packet is received. 0: none; 1: above; 2: below */
    unsigned :8;                  /**< reserved */
    unsigned timestamp:32;        /**< timestamp. The local time when this packet is received. It is precise only if modem sleep or light sleep is not enabled. unit: microsecond */
    unsigned :32;                 /**< reserved */
    unsigned :31;                 /**< reserved */
    unsigned ant:1;               /**< antenna number from which this packet is received. 0: WiFi antenna 0; 1: WiFi antenna 1 */
    unsigned sig_len:12;          /**< length of packet including Frame Check Sequence(FCS) */
    unsigned sig_len_copy:12;     /**< another copy of the length */
    unsigned rx_state:8;          /**< state of the packet. 0: no error; others: error numbers which are not public */
} QEMU_PACKED wifi_pkt_rx_ctrl_t;

extern int esp32_wifi_channel;

typedef uint8_t ip4_t[4];

#define DHCP_CHADDR_LEN 16
#define DHCP_SNAME_LEN  64
#define DHCP_FILE_LEN   128

typedef struct ip_header_t {
    uint8_t version_size;
    uint8_t dscp_ecn;
    uint8_t len_h;
    uint8_t len_l;
    uint32_t z;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    ip4_t source_ip;
    ip4_t dest_ip;
}ip_header_t ;

typedef struct udp_header_t {
    uint8_t src_port_h;
    uint8_t src_port_l;
    uint8_t dest_port_h;
    uint8_t dest_port_l;
    uint8_t len_h;
    uint8_t len_l;
    uint16_t checksum;
} udp_header_t;

typedef struct dhcp_t {
    uint8_t    opcode;
    uint8_t    htype;
    uint8_t    hlen;
    uint8_t    hops;
    uint32_t   xid;
    uint16_t   secs;
    uint16_t   flags;
    ip4_t       ciaddr;
    ip4_t       yiaddr;
    ip4_t       siaddr;
    ip4_t       giaddr;
    uint8_t     chaddr[DHCP_CHADDR_LEN];
    char        bp_sname[DHCP_SNAME_LEN];
    char        bp_file[DHCP_FILE_LEN];
    uint32_t    magic_cookie;
    uint8_t     bp_options[0];
} dhcp_t;

typedef struct dhcp_request_t {
    ip_header_t ipheader;
    udp_header_t udpheader;
    dhcp_t dhcp;
} dhcp_request_t;

#endif // esp32_wlan_h

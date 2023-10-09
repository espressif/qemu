/**
 * QEMU WLAN access point emulation
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

#ifndef esp32_wlan_packet_h
#define esp32_wlan_packet_h 1

#include "esp32_wlan.h"
#include "hw/misc/esp32_wifi.h"

void Esp32_WLAN_init_ap_frame(Esp32WifiState *s, struct mac80211_frame *frame);
int Esp32_WLAN_dumpFrame(struct mac80211_frame *frame, int frame_len, char *filename);
void Esp32_WLAN_insert_frame(Esp32WifiState *s, struct mac80211_frame *frame);
struct mac80211_frame *Esp32_WLAN_create_beacon_frame(access_point_info *ap);
struct mac80211_frame *Esp32_WLAN_create_probe_response(access_point_info *ap);
struct mac80211_frame *Esp32_WLAN_create_probe_request(access_point_info *ap);
struct mac80211_frame *Esp32_WLAN_create_authentication_request(void);
struct mac80211_frame *Esp32_WLAN_create_authentication_response(access_point_info *ap);
struct mac80211_frame *Esp32_WLAN_create_deauthentication(void);
struct mac80211_frame *Esp32_WLAN_create_association_request(access_point_info *ap);
struct mac80211_frame *Esp32_WLAN_create_association_response(access_point_info *ap);
struct mac80211_frame *Esp32_WLAN_create_disassociation(void);
struct mac80211_frame *Esp32_WLAN_create_data_reply(Esp32WifiState *s, struct mac80211_frame *incoming);
struct mac80211_frame *Esp32_WLAN_create_data_packet(Esp32WifiState *s, const uint8_t *buf, int size);
struct mac80211_frame *Esp32_WLAN_create_ack(void);
struct mac80211_frame *Esp32_WLAN_create_dhcp_discover(void);
struct mac80211_frame *Esp32_WLAN_create_dhcp_request(uint8_t *ip);
void insertCRC(mac80211_frame *frame);

#endif // esp32_wlan_packet_h

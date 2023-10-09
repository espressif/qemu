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
 */

#include "qemu/osdep.h"
#include "net/net.h"
#include "qemu/timer.h"

#include "hw/misc/esp32_wifi.h"
#include "esp32_wlan.h"
#include "esp32_wlan_packet.h"

// 50ms between beacons
#define BEACON_TIME 50000000
#define INTER_FRAME_TIME 5000000
#define DEBUG 1
#define DEBUG_DUMPFRAMES 1

// channel 12, 13 and 14 aren't scanned with probe requests, but by listening to beacons
// likely because those channels aren't freely licensed in all countries
access_point_info access_points[]={
    {"Open Wifi",4,-40,{0x10,0x01,0x00,0xc4,0x0a,0x51}},
    {"meshtest",1,-25,{0x10,0x01,0x00,0xc4,0x0a,0x50}},
    {"Zeus WPI",12,-25,{0x10,0x01,0x00,0xc4,0x0a,0x56}}
};

int nb_aps=sizeof(access_points)/sizeof(access_point_info);

static void Esp32_WLAN_beacon_timer(void *opaque)
{
    struct mac80211_frame *frame;
    Esp32WifiState *s = (Esp32WifiState *)opaque;

    // only send a beacon if we are an access point
    if(s->ap_state!=Esp32_WLAN__STATE_STA_ASSOCIATED) {
        if (access_points[s->beacon_ap].channel==esp32_wifi_channel) {
            printf("QEMU: sending beacon for AP %s\n", access_points[s->beacon_ap].ssid);
            memcpy(s->ap_macaddr,access_points[s->beacon_ap].mac_address,6);
            frame = Esp32_WLAN_create_beacon_frame(&access_points[s->beacon_ap]);
            memcpy(frame->receiver_address, BROADCAST, 6);
            memcpy(frame->transmitter_address, s->ap_macaddr, 6);
            memcpy(frame->address_3, s->ap_macaddr, 6);
            Esp32_WLAN_init_ap_frame(s, frame);
            Esp32_WLAN_insert_frame(s, frame);
        }
        s->beacon_ap=(s->beacon_ap+1)%nb_aps;
    }
    timer_mod(s->beacon_timer, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + BEACON_TIME);
}

static void Esp32_WLAN_inject_timer(void *opaque)
{
    Esp32WifiState *s = (Esp32WifiState *)opaque;
    struct mac80211_frame *frame;

    frame = s->inject_queue;
    if (frame) {
        // remove from queue
        s->inject_queue_size--;
        s->inject_queue = frame->next_frame;
        Esp32_sendFrame(s, (void *)frame, frame->frame_length,frame->signal_strength);
        free(frame);
    }
    if (s->inject_queue_size > 0) {
        // there are more packets... schedule
        // the timer for sending them as well
        timer_mod(s->inject_timer, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + INTER_FRAME_TIME);
    } else {
        // we wait until a new packet schedules
        // us again
        s->inject_timer_running = 0;
    }

}

static void macprint(uint8_t *p, const char * name) {
    printf("%s: %02x:%02x:%02x:%02x:%02x:%02x\n",name, p[0],p[1],p[2],p[3],p[4],p[5]);
}

static void infoprint(struct mac80211_frame *frame) {
    if(DEBUG_DUMPFRAMES) {
        printf("Frame Info type=%d subtype=%d to_ds=%d from_ds=%d duration=%d frame_length=%d\n",frame->frame_control.type,frame->frame_control.sub_type, frame->frame_control.to_ds,frame->frame_control.from_ds, frame->duration_id, frame->frame_length);
        macprint(frame->receiver_address,   "receiver   ");
        macprint(frame->transmitter_address,"transmitter");
        macprint(frame->address_3,          "3rd address");
        uint8_t *b=(uint8_t *)frame;
        for(int i=0;i<frame->frame_length;i++) {
            if((i%16)==0) printf("\n%04x: ",i);
            printf("%02x ",b[i]);
        }
        printf("\n");
    }
}

void Esp32_WLAN_insert_frame(Esp32WifiState *s, struct mac80211_frame *frame)
{
    struct mac80211_frame *i_frame;

    insertCRC(frame);
    if(DEBUG) printf("QEMU: sent frame (qemu AP -> ESP32) type=%d subtype=%d\n",frame->frame_control.type,frame->frame_control.sub_type);
    infoprint(frame);
    s->inject_queue_size++;
    i_frame = s->inject_queue;
    if (!i_frame) {
        s->inject_queue = frame;
    } else {
        while (i_frame->next_frame) {
            i_frame = i_frame->next_frame;
        }
        i_frame->next_frame = frame;
    }

    if (!s->inject_timer_running) {
        // if the injection timer is not
        // running currently, let's schedule
        // one run...
        s->inject_timer_running = 1;
        timer_mod(s->inject_timer, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + INTER_FRAME_TIME);
    }

}

static _Bool Esp32_WLAN_can_receive(NetClientState *ncs)
{
    Esp32WifiState *s = qemu_get_nic_opaque(ncs);

    if (s->ap_state != Esp32_WLAN__STATE_ASSOCIATED  && s->ap_state != Esp32_WLAN__STATE_STA_ASSOCIATED) {
        // we are currently not connected
        // to the access point
        return 0;
    }
    if (s->inject_queue_size > Esp32_WLAN__MAX_INJECT_QUEUE_SIZE) {
        // overload, please give me some time...
        return 0;
    }

    return 1;
}

static ssize_t Esp32_WLAN_receive(NetClientState *ncs,
                                    const uint8_t *buf, size_t size)
{
    Esp32WifiState *s = qemu_get_nic_opaque(ncs);
    struct mac80211_frame *frame;
    if (!Esp32_WLAN_can_receive(ncs)) {
        // this should not happen, but in
        // case it does, let's simply drop
        // the packet
        return -1;
    }

    if (!s) {
        return -1;
    }
    /*
     * A 802.3 packet comes from the qemu network. The
     * access points turns it into a 802.11 frame and
     * forwards it to the wireless device
     */
    frame = Esp32_WLAN_create_data_packet(s, buf, size);
    if (frame) {
        /* send message to ESP32 AP */
        if(s->ap_state == Esp32_WLAN__STATE_STA_ASSOCIATED) {
            printf("QEMU: Esp32_WLAN_create_data_packet not yet implemented for STA!");
            frame->frame_control.to_ds = 1;
            frame->frame_control.from_ds = 0;
            memcpy(frame->receiver_address, s->ap_macaddr, 6); // ?
            // TODO implement setting all 3 802.11 MAC addresses
        }
        else { // send message to ESP32 station
            frame->frame_control.to_ds = 0;
            frame->frame_control.from_ds = 1;
            memcpy(frame->receiver_address, &buf[0], 6);
            memcpy(frame->transmitter_address, s->associated_ap_macaddr, 6);
            memcpy(frame->address_3, &buf[6], 6); // source address
        }
        Esp32_WLAN_init_ap_frame(s, frame);
        Esp32_WLAN_insert_frame(s, frame);
    }
    return size;
}
static void Esp32_WLAN_cleanup(NetClientState *ncs) { }

static NetClientInfo net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = Esp32_WLAN_can_receive,
    .receive = Esp32_WLAN_receive,
    .cleanup = Esp32_WLAN_cleanup,
};

void Esp32_WLAN_setup_ap(DeviceState *dev,Esp32WifiState *s) {

    s->ap_state = Esp32_WLAN__STATE_NOT_AUTHENTICATED;
    s->beacon_ap=0;
    memcpy(s->ap_macaddr,(uint8_t[]){0x01,0x13,0x46,0xbf,0x31,0x50},sizeof(s->ap_macaddr));
    memcpy(s->macaddr,(uint8_t[]){0x10,0x01,0x00,0xc4,0x0a,0x24},sizeof(s->macaddr));

    s->inject_timer_running = 0;
    s->inject_sequence_number = 0;

    s->inject_queue = NULL;
    s->inject_queue_size = 0;

    s->beacon_timer = timer_new_ns(QEMU_CLOCK_REALTIME, Esp32_WLAN_beacon_timer, s);
    timer_mod(s->beacon_timer, qemu_clock_get_ns(QEMU_CLOCK_REALTIME)+100000000);

    // setup the timer but only schedule
    // it when necessary...
    s->inject_timer = timer_new_ns(QEMU_CLOCK_REALTIME, Esp32_WLAN_inject_timer, s);

    s->nic = qemu_new_nic(&net_info, &s->conf, object_get_typename(OBJECT(s)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->macaddr);
}

static void send_single_frame(Esp32WifiState *s, struct mac80211_frame *frame, struct mac80211_frame *reply) {
    reply->sequence_control.sequence_number = s->inject_sequence_number++ +0x730;
    reply->signal_strength=-10;

    if(frame) {
        memcpy(reply->receiver_address, frame->transmitter_address, 6);
        memcpy(reply->transmitter_address, s->macaddr, 6);
        memcpy(reply->address_3, frame->transmitter_address, 6);
    }

    Esp32_WLAN_insert_frame(s, reply);
}
void Esp32_WLAN_handle_frame(Esp32WifiState *s, struct mac80211_frame *frame)
{
    struct mac80211_frame *reply = NULL;
    static access_point_info dummy_ap={0};
    char ssid[64];
    unsigned long ethernet_frame_size;
    unsigned char ethernet_frame[1518] = {0};
    if(DEBUG)
        printf("QEMU: received frame (esp32 -> qemu) type=%d subtype=%d chan=%d to_ds=%d from_ds=%d state=%d\n",frame->frame_control.type, frame->frame_control.sub_type, esp32_wifi_channel, frame->frame_control.to_ds, frame->frame_control.from_ds, s->ap_state);
    infoprint(frame);
    access_point_info *ap_info=0;
    for (int i=0;i<nb_aps;i++) {
        if (access_points[i].channel == esp32_wifi_channel) {
            if (DEBUG) printf("QEMU: matching ap found: %s\n", access_points[i].ssid);
            ap_info=&access_points[i];
        }
    }

    if(frame->frame_control.type == IEEE80211_TYPE_MGT) {
        switch(frame->frame_control.sub_type) {
            case IEEE80211_TYPE_MGT_SUBTYPE_BEACON:
                if(s->ap_state==Esp32_WLAN__STATE_NOT_AUTHENTICATED || s->ap_state==Esp32_WLAN__STATE_AUTHENTICATED) {
                    strncpy(ssid,(char *)frame->data_and_fcs+14,frame->data_and_fcs[13]);
                    if(DEBUG) printf("QEMU: beacon from %s\n",ssid);
                    dummy_ap.ssid=ssid;
                    s->ap_state=Esp32_WLAN__STATE_STA_NOT_AUTHENTICATED;
                    send_single_frame(s,frame,Esp32_WLAN_create_probe_request(&dummy_ap));
                }
                break;
            case IEEE80211_TYPE_MGT_SUBTYPE_PROBE_RESP:
                ap_info=&dummy_ap;
                strncpy(ssid,(char *)frame->data_and_fcs+14,frame->data_and_fcs[13]);
                if(DEBUG) printf("QEMU: probe resp from %s\n",ssid);
                dummy_ap.ssid=ssid;
                s->ap_state=Esp32_WLAN__STATE_STA_NOT_AUTHENTICATED;
                send_single_frame(s,frame,Esp32_WLAN_create_deauthentication());
                send_single_frame(s,frame,Esp32_WLAN_create_authentication_request());
                break;
            case IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_RESP:
                if(DEBUG) printf("QEMU: assoc resp\n");
                mac80211_frame *frame1=Esp32_WLAN_create_dhcp_discover();
                memcpy(frame1->address_3,BROADCAST,6);
                memcpy(frame1->transmitter_address,frame->receiver_address,6);
                memcpy(frame1->receiver_address,frame->transmitter_address,6);
                send_single_frame(s,0,frame1);
                s->ap_state=Esp32_WLAN__STATE_STA_DHCP;
                break;
            case IEEE80211_TYPE_MGT_SUBTYPE_DISASSOCIATION:
                DEBUG_PRINT_AP(("QEMU: Received disassociation!\n"));
                send_single_frame(s,frame,Esp32_WLAN_create_disassociation());
                if (s->ap_state == Esp32_WLAN__STATE_ASSOCIATED || s->ap_state == Esp32_WLAN__STATE_STA_ASSOCIATED) {
                    s->ap_state = Esp32_WLAN__STATE_AUTHENTICATED;
                }
                break;
            case IEEE80211_TYPE_MGT_SUBTYPE_DEAUTHENTICATION:
                DEBUG_PRINT_AP(("QEMU: Received deauthentication!\n"));
                //reply = Esp32_WLAN_create_authentication_response(ap_info);
                if (s->ap_state == Esp32_WLAN__STATE_AUTHENTICATED) {
                    s->ap_state = Esp32_WLAN__STATE_NOT_AUTHENTICATED;
                }
                break;
            case IEEE80211_TYPE_MGT_SUBTYPE_AUTHENTICATION:
                if(frame->data_and_fcs[2]==2) { // response
                    DEBUG_PRINT_AP(("QEMU: Received authentication response!\n"));
                    send_single_frame(s,frame,Esp32_WLAN_create_association_request(&dummy_ap));
                }
                break;
        }
        if(ap_info) {
            memcpy(s->ap_macaddr, ap_info->mac_address, 6);
            switch(frame->frame_control.sub_type) {
                case IEEE80211_TYPE_MGT_SUBTYPE_PROBE_REQ:
                    DEBUG_PRINT_AP(("QEMU: Received probe request!\n"));
                    reply = Esp32_WLAN_create_probe_response(ap_info);
                    break;
                case IEEE80211_TYPE_MGT_SUBTYPE_AUTHENTICATION:
                    if(frame->data_and_fcs[2]==1) { // request
                        DEBUG_PRINT_AP(("QEMU: Received authentication request!\n"));
                        reply = Esp32_WLAN_create_authentication_response(ap_info);
                        if (s->ap_state == Esp32_WLAN__STATE_NOT_AUTHENTICATED) {
                            s->ap_state = Esp32_WLAN__STATE_AUTHENTICATED;
                        }
                    }
                break;
                case IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_REQ:
                    DEBUG_PRINT_AP(("QEMU: Received association request!\n"));
                    reply = Esp32_WLAN_create_association_response(ap_info);
                    if (s->ap_state == Esp32_WLAN__STATE_AUTHENTICATED) {
                        s->ap_state = Esp32_WLAN__STATE_ASSOCIATED;
                        memcpy(s->associated_ap_macaddr,s->ap_macaddr,6);
                    }
                    break;
            }
            if (reply) {
                reply->signal_strength=ap_info->sigstrength;
                memcpy(reply->receiver_address, frame->transmitter_address, 6);
                memcpy(reply->transmitter_address, s->ap_macaddr, 6);
                memcpy(reply->address_3, s->ap_macaddr, 6);
                Esp32_WLAN_init_ap_frame(s, reply);
                Esp32_WLAN_insert_frame(s, reply);
            }
        }
    }
    if ((frame->frame_control.type == IEEE80211_TYPE_DATA) &&
        (frame->frame_control.sub_type == IEEE80211_TYPE_DATA_SUBTYPE_DATA)) {
        if(s->ap_state == Esp32_WLAN__STATE_STA_DHCP) {
            printf("QEMU: STA DHCP not implemented yet\n");
            dhcp_request_t *req=(dhcp_request_t *)&frame->data_and_fcs[8];
            // check for a dhcp offer
            if(req->dhcp.bp_options[0]==0x35 && req->dhcp.bp_options[2]==0x2) {
                mac80211_frame *frame1=Esp32_WLAN_create_dhcp_request(req->dhcp.yiaddr);
                memcpy(frame1->address_3,BROADCAST,6);
                memcpy(frame1->transmitter_address,s->macaddr,6);
                memcpy(frame1->receiver_address,frame->transmitter_address,6);
                send_single_frame(s,0,frame1);
                memcpy(s->ap_macaddr,(uint8_t[]){0x10,0x01,0x00,0xc4,0x0a,0x25},sizeof(s->ap_macaddr));
                memcpy(s->macaddr,(uint8_t[]){0x10,0x01,0x00,0xc4,0x0a,0x24},sizeof(s->macaddr));
                memcpy(s->associated_ap_macaddr,s->ap_macaddr,sizeof(s->ap_macaddr));
                s->ap_state=Esp32_WLAN__STATE_STA_ASSOCIATED;
            }
        } else if (s->ap_state == Esp32_WLAN__STATE_ASSOCIATED) {
            // ESP32 to QEMU
            /*
            * The access point uses the 802.11 frame
            * and sends a 802.3 frame into the network...
            * This packet is then understandable by
            * qemu-slirp
            *
            * If we ever want the access point to offer
            * some services, it can be added here!!
            */
            // ethernet header type
            ethernet_frame[12] = frame->data_and_fcs[6];
            ethernet_frame[13] = frame->data_and_fcs[7];

            // the originator the packet is the station who sent the frame
            memcpy(&ethernet_frame[6], frame->transmitter_address, 6);

            // in case of to_ds = 1 and from_ds = 0, the third address is the destination address
            memcpy(&ethernet_frame[0], frame->address_3, 6);

            ethernet_frame_size = frame->frame_length - 22;

            // limit data to max length of ethernet frame
            if (ethernet_frame_size > (sizeof(ethernet_frame) - 14)) {
                ethernet_frame_size = (sizeof(ethernet_frame) - 14);
            }

            // set ethernet data
            memcpy(&ethernet_frame[14], &frame->data_and_fcs[8], ethernet_frame_size);

            // send frame
            qemu_send_packet(qemu_get_queue(s->nic), ethernet_frame, ethernet_frame_size);
        } else if (s->ap_state == Esp32_WLAN__STATE_STA_ASSOCIATED) {
            printf("QEMU: STA DATA, NOT IMPLEMENTED YET\n");
        }
    }
}

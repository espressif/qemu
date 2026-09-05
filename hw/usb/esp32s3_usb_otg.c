/*
 * ESP32-S3 USB-OTG (Synopsys DWC2, device mode) emulation
 *
 * See include/hw/usb/esp32s3_usb_otg.h for the design overview.
 *
 * Implemented register semantics follow the Synopsys DWC2 databook as
 * driven by TinyUSB's dcd_dwc2 driver (slave/FIFO mode, device role):
 *  - global/core regs (GOTGCTL..GDFIFOCFG), GSNPSID/GHWCFG1-4 as the
 *    ESP32-S3 FS-only, slave-only core reports them
 *  - device regs (DCFG/DCTL/DSTS), endpoint interrupt plumbing
 *    (DIEPMSK/DOEPMSK/DAINT/DAINTMSK/DIEPEMPMSK)
 *  - IN/OUT endpoint register blocks (DxEPCTL/INT/TSIZ/DTXFSTS)
 *  - word-granular DFIFO windows (4 bytes per 32-bit access); DFIFO0
 *    writes feed the EP0 IN Tx FIFO, DFIFO0 reads pop the shared Rx FIFO
 *  - GRXSTSP status queue driving the RXFLVL interrupt
 *
 * The virtual host raises USBRST+ENUMDNE after the guest connects, then
 * runs the standard enumeration sequence, parsing the guest's descriptors
 * (device descriptor string indexes, config wTotalLength). After
 * SET_CONFIGURATION it exchanges bulk data with the chardev: guest IN
 * packets are written to the chardev; chardev bytes are delivered as OUT
 * packets (one <=MPS packet per arming, plus a ZLP once the pending data
 * drains, so guest reads complete per host write boundary).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/usb/esp32s3_usb_otg.h"
#include "hw/misc/esp32s3_reg.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"

#define DEBUG_DWC2 1

#ifdef DEBUG_DWC2
#define DWC2_DEBUG(...) qemu_log(__VA_ARGS__)
#else
#define DWC2_DEBUG(...) do { } while (0)
#endif

/* Register offsets (device-mode subset) */
#define R_GOTGCTL      0x000
#define R_GOTGINT      0x004
#define R_GAHBCFG      0x008
#define R_GUSBCFG      0x00C
#define R_GRSTCTL      0x010
#define R_GINTSTS      0x014
#define R_GINTMSK      0x018
#define R_GRXSTSR      0x01C
#define R_GRXSTSP      0x020
#define R_GRXFSIZ      0x024
#define R_DIEPTXF0     0x028
#define R_GNPTXSTS     0x02C
#define R_GGPIO        0x038
#define R_GUID         0x03C
#define R_GSNPSID      0x040
#define R_GHWCFG1      0x044
#define R_GHWCFG2      0x048
#define R_GHWCFG3      0x04C
#define R_GHWCFG4      0x050
#define R_GDFIFOCFG    0x05C
#define R_DIEPTXF0_N   0x104     /* DIEPTXFn: n = 0..14 -> IN EP n+1 */
#define R_DIEPTXF_LAST 0x13C

#define R_DCFG         0x800
#define R_DCTL         0x804
#define R_DSTS         0x808
#define R_DIEPMSK      0x810
#define R_DOEPMSK      0x814
#define R_DAINT        0x818
#define R_DAINTMSK     0x81C
#define R_DIEPEMPMSK   0x834
#define R_PCGCCTL      0xE00

#define R_EPIN_BASE    0x900    /* IN EPn block: 0x900 + 0x20*n */
#define R_EPOUT_BASE   0xB00    /* OUT EPn block: 0xB00 + 0x20*n */
#define EP_OFF_CTL     0x00
#define EP_OFF_INT     0x08
#define EP_OFF_TSIZ    0x10
#define EP_OFF_DMA     0x14
#define EP_OFF_DTXFSTS 0x18
#define EP_BLK_SIZE    0x20

#define R_DFIFO0       0x1000
#define R_DFIFO_STRIDE 0x1000
#define R_DFIFO_END    0x10000

/* Bit definitions */
#define GINT_B_SOF         (1u << 3)
#define GINT_B_RXFLVL      (1u << 4)
#define GINT_B_GOUTNAKEFF  (1u << 7)
#define GINT_B_USBRST      (1u << 12)
#define GINT_B_ENUMDNE     (1u << 13)
#define GINT_B_IEPINT      (1u << 18)
#define GINT_B_OEPINT      (1u << 19)

/* GRXSTSP packet status values */
#define PKTSTS_RX_DATA     2
#define PKTSTS_RX_COMPLETE 3
#define PKTSTS_SETUP_DONE  4
#define PKTSTS_SETUP_RX    6

/* DEPCTL bits */
#define EPCTL_MPSIZ_MASK   0x7FFu
#define EPCTL_USBAEP       (1u << 15)
#define EPCTL_STALL        (1u << 21)
#define EPCTL_CNAK         (1u << 26)
#define EPCTL_SNAK         (1u << 27)
#define EPCTL_EPDIS        (1u << 30)
#define EPCTL_EPENA        (1u << 31)

/* DIEPINT bits */
#define DIEPINT_XFRC       (1u << 0)
#define DIEPINT_EPDISD     (1u << 1)
#define DIEPINT_TXFE       (1u << 7)

/* DOEPINT bits */
#define DOEPINT_XFRC       (1u << 0)
#define DOEPINT_EPDISD     (1u << 1)
#define DOEPINT_SETUP      (1u << 3)

/* GRSTCTL bits */
#define GRSTCTL_CSRST      (1u << 0)
#define GRSTCTL_RXFFLSH    (1u << 4)
#define GRSTCTL_TXFFLSH    (1u << 5)
#define GRSTCTL_TXFNUM_SHIFT 6
#define GRSTCTL_TXFNUM_MASK   (0x1Fu << GRSTCTL_TXFNUM_SHIFT)
#define GRSTCTL_AHBIDL     (1u << 31)

/* DCTL bits */
#define DCTL_SDIS          (1u << 1)
#define DCTL_SGONAK        (1u << 9)
#define DCTL_CGONAK        (1u << 10)

/* tsiz fields */
#define TSIZ_XFER_SIZE_MASK 0x7FFFFu

/* GRXSTSP entry composition */
#define GRXSTSP_ENTRY(epnum, bcnt, pktsts) \
    (((epnum) & 0xFu) | (((bcnt) & 0x7FFu) << 4) | (((pktsts) & 0xFu) << 17))

/* Identity values reported by the emulated core.
 * GSNPSID: DWC2 OTG family, rev 2.80a (< 3.00a so the DMA setup path is
 * never taken, != 3.10a so the RXFIFO quirk path is never taken, < 4.20a
 * so CSRST is expected to be self-clearing, which this model implements).
 * GHWCFG2: slave-only arch, no HS PHY, dedicated FS PHY, 6 device
 * endpoints (+EP0), dynamic FIFO sizing.
 * GHWCFG3: 19-bit xfer size counter, 10-bit packet size counter,
 * 256-word DFIFO depth.
 * GHWCFG4: dedicated tx FIFOs, 6 IN endpoints incl. EP0.
 */
#define GSNPSID_VAL  0x4F54280Au
#define GHWCFG1_VAL  0x00000556u /* EP0 bidi, EP1-5 IN, EP6 OUT */
#define GHWCFG2_VAL  0x00081900u
#define GHWCFG3_VAL  0x01000068u
#define GHWCFG4_VAL  0x1A000000u

#define EP0_MPS           64

/* Virtual host timing (virtual-time milliseconds). The step timeout is
 * generous: a real host waits seconds for control responses. */
#define VHOST_RESET_DELAY_MS   5
#define VHOST_ENUM_DELAY_MS    5
#define VHOST_STEP_TIMEOUT_MS  20000
#define VHOST_FRAME_PERIOD_MS  1

/* Largest OUT packet delivered per arming (FS bulk MPS) */
#define OUT_PACKET_MAX 64

static inline uint32_t tsiz_xfer_size(uint32_t tsiz)
{
    return tsiz & TSIZ_XFER_SIZE_MASK;
}

static inline uint32_t dep_mps(uint32_t ctl, unsigned epnum)
{
    if (epnum == 0) {
        /* EP0 MPSIZ is a 2-bit encoding at bits [21:22]: 0=64 1=32 2=16 3=8 */
        switch ((ctl >> 21) & 0x3u) {
        case 0: return 64;
        case 1: return 32;
        case 2: return 16;
        default: return 8;
        }
    }
    return ctl & EPCTL_MPSIZ_MASK;
}

/* Derived DAINT: per-endpoint interrupt summary (IN low half, OUT high). */
static uint32_t dwc2_daint_derived(Esp32s3UsbOtgState *s)
{
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < ESP32S3_USB_EP_COUNT; i++) {
        /*
         * TXFE is gated by the per-endpoint DIEPEMPMSK bit, not DIEPMSK
         * (real Synopsys DWC2 semantics: DIEPMSK's own TXFE-adjacent bit
         * is a different, common-interrupt-only gate; drivers that ask
         * for a one-shot "FIFO empty" notification around each transfer
         * use DIEPEMPMSK per endpoint instead). Without this, a TXFE
         * assert never reaches DAINT/GINTSTS.IEPINT for a driver that
         * only ever unmasks it via DIEPEMPMSK — the transfer stays armed
         * with no interrupt ever telling the driver to push data.
         */
        if (s->epin[i].int_st & s->diepmsk & ~DIEPINT_TXFE) {
            v |= 1u << i;
        }
        if ((s->epin[i].int_st & DIEPINT_TXFE) && (s->diepempmsk & (1u << i))) {
            v |= 1u << i;
        }
        if (s->epout[i].int_st & s->doepmsk) {
            v |= 1u << (16 + i);
        }
    }
    return v;
}

static uint32_t dwc2_gintsts_effective(Esp32s3UsbOtgState *s)
{
    uint32_t v = s->gintsts;
    uint32_t daint = dwc2_daint_derived(s);

    if (s->rxstsp_head != s->rxstsp_tail) {
        v |= GINT_B_RXFLVL;
    }
    if (daint & s->daintmsk & 0xFFFFu) {
        v |= GINT_B_IEPINT;
    }
    if (daint & s->daintmsk & 0xFFFF0000u) {
        v |= GINT_B_OEPINT;
    }
    return v;
}

static void dwc2_update_irq(Esp32s3UsbOtgState *s)
{
    bool level = ((dwc2_gintsts_effective(s) & s->gintmsk) != 0)
                  && (s->gahbcfg & 1u); /* GAHBCFG.GINT */
    qemu_set_irq(s->irq, level);
}

/* Chardev data path */
static void dwc2_ch_flush(Esp32s3UsbOtgState *s);
static void dwc2_out_pump(Esp32s3UsbOtgState *s);

static gboolean dwc2_ch_flush_cb(void *do_not_use, GIOCondition cond, void *opaque)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(opaque);

    (void) do_not_use;
    (void) cond;
    s->tx_watch = 0;
    dwc2_ch_flush(s);
    return G_SOURCE_REMOVE;
}

static void dwc2_ch_flush(Esp32s3UsbOtgState *s)
{
    while (!fifo8_is_empty(&s->tx_ch_fifo)) {
        uint32_t navail = 0;
        const uint8_t *buf = fifo8_peek_bufptr(&s->tx_ch_fifo, fifo8_num_used(&s->tx_ch_fifo),
                                                &navail);
        int r = qemu_chr_fe_write(&s->chr, buf, navail);

        if (r <= 0) {
            if (!s->tx_watch) {
                s->tx_watch = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                                    dwc2_ch_flush_cb, s);
            }
            break;
        }
        fifo8_drop(&s->tx_ch_fifo, r);
        if ((uint32_t)r < navail) {
            break;
        }
    }
}

static int dwc2_ch_can_receive(void *opaque)
{
    return 4096;
}

static void dwc2_ch_receive(void *opaque, const uint8_t *buf, int size)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(opaque);

    fifo8_push_all(&s->rx_pending, buf, size);
    /* an armed OUT endpoint may be waiting for exactly this data */
    dwc2_out_pump(s);
}

static void dwc2_ch_event(void *opaque, QEMUChrEvent event)
{
}

/* GRXSTSP status queue (circular, one 32-bit word per entry) */
static void dwc2_rxstsp_push(Esp32s3UsbOtgState *s, unsigned epnum,
                             unsigned bcnt, unsigned pktsts)
{
    uint32_t next = (s->rxstsp_head + 1) % ESP32S3_USB_RXSTSP_QLEN;

    if (next == s->rxstsp_tail) {
        qemu_log_mask(LOG_GUEST_ERROR, "dwc2: GRXSTSP queue overflow\n");
        return;
    }
    s->rxstsp_q[s->rxstsp_head] = GRXSTSP_ENTRY(epnum, bcnt, pktsts);
    s->rxstsp_head = next;
}

static uint32_t dwc2_rxstsp_peek(Esp32s3UsbOtgState *s)
{
    if (s->rxstsp_head == s->rxstsp_tail) {
        return 0;
    }
    return s->rxstsp_q[s->rxstsp_tail];
}

static void dwc2_rxstsp_pop(Esp32s3UsbOtgState *s)
{
    if (s->rxstsp_head != s->rxstsp_tail) {
        s->rxstsp_tail = (s->rxstsp_tail + 1) % ESP32S3_USB_RXSTSP_QLEN;
    }
}

/* Word-aligned push into the shared RX data FIFO (reads pop 4 bytes). */
static void dwc2_rx_fifo_push(Esp32s3UsbOtgState *s, const uint8_t *data, unsigned len)
{
    unsigned padded = (len + 3u) & ~3u;

    fifo8_push_all(&s->grx_fifo, data, len);
    while (len < padded) {
        fifo8_push(&s->grx_fifo, 0);
        len++;
    }
}

static uint32_t dwc2_rx_fifo_pop_word(Esp32s3UsbOtgState *s)
{
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < 4; i++) {
        if (fifo8_is_empty(&s->grx_fifo)) {
            break;
        }
        v |= (uint32_t)fifo8_pop(&s->grx_fifo) << (8 * i);
    }
    return v;
}

/*
 * Move data from rx_pending into armed OUT endpoints. A packet (<= MPS)
 * is delivered per iteration; a short packet or an exhausted transfer
 * size completes it (RX_COMPLETE + DOEPINT.XFRC). When pending data
 * drains after a full packet while the transfer wants more, a ZLP is
 * delivered so the guest read completes at the host write boundary.
 */
static void dwc2_out_pump(Esp32s3UsbOtgState *s)
{
    unsigned ep;

    for (ep = 1; ep < ESP32S3_USB_EP_COUNT; ep++) {
        uint32_t ctl = s->epout[ep].ctl;
        bool delivered_any = false;
        bool complete = false;

        if (!(ctl & EPCTL_EPENA) || (ctl & EPCTL_STALL)) {
            continue;
        }

        while (!fifo8_is_empty(&s->rx_pending)) {
            uint32_t remain = tsiz_xfer_size(s->epout[ep].tsiz);
            uint32_t mps = dep_mps(ctl, ep);
            uint32_t navail = 0;
            uint32_t n, chunk;
            const uint8_t *src;

            n = MIN(remain, mps);
            if (n == 0) {
                break;
            }
            if (fifo8_num_used(&s->rx_pending) < n) {
                n = fifo8_num_used(&s->rx_pending);
            }

            src = fifo8_peek_bufptr(&s->rx_pending, n, &navail);
            chunk = MIN(n, navail);
            /* RX_DATA status word first: the driver pops it, then reads
             * exactly `bcnt` bytes off the shared RX data FIFO */
            dwc2_rxstsp_push(s, ep, chunk, PKTSTS_RX_DATA);
            dwc2_rx_fifo_push(s, src, chunk);
            fifo8_drop(&s->rx_pending, chunk);

            /* hardware decrements DOEPTSIZ as bytes are received */
            s->epout[ep].tsiz -= chunk;
            delivered_any = true;
            DWC2_DEBUG("dwc2: OUT ep%u %u bytes (remain %u)\n",
                       ep, chunk, tsiz_xfer_size(s->epout[ep].tsiz));

            if (chunk < mps || tsiz_xfer_size(s->epout[ep].tsiz) == 0) {
                complete = true;
                break;
            }
        }

        if (complete) {
            dwc2_rxstsp_push(s, ep, 0, PKTSTS_RX_COMPLETE);
            s->epout[ep].int_st |= DOEPINT_XFRC;
            s->epout[ep].ctl &= ~EPCTL_EPENA;
            dwc2_update_irq(s);
        } else if (delivered_any) {
            /* drained after full packet(s): deliver a ZLP to complete */
            dwc2_rxstsp_push(s, ep, 0, PKTSTS_RX_DATA);
            dwc2_rxstsp_push(s, ep, 0, PKTSTS_RX_COMPLETE);
            s->epout[ep].int_st |= DOEPINT_XFRC;
            s->epout[ep].ctl &= ~EPCTL_EPENA;
            dwc2_update_irq(s);
        }
    }
}

/* Virtual host: enumeration + control transfers */
/* Build the SETUP packet for `seq`; returns NULL when enumeration ends. */
static const uint8_t *vhost_make_setup(Esp32s3UsbOtgState *s, unsigned seq)
{
    uint8_t *p = s->vhost_setup;
    unsigned idx;

    memset(p, 0, 8);
    switch (seq) {
    case 0: /* GET_DESCRIPTOR device, 64 */
        p[0] = 0x80; p[1] = 0x06; p[3] = 0x01;
        p[6] = 64;
        return p;
    case 1: /* SET_ADDRESS 1 */
        p[1] = 0x05; p[2] = 0x01;
        return p;
    case 2: /* GET_DESCRIPTOR device, 18 */
        p[0] = 0x80; p[1] = 0x06; p[3] = 0x01;
        p[6] = 18;
        return p;
    case 3: /* GET_DESCRIPTOR config, 9 */
        p[0] = 0x80; p[1] = 0x06; p[3] = 0x02;
        p[6] = 9;
        return p;
    case 4: /* GET_DESCRIPTOR config, wTotalLength */
        if (s->vhost_cfg_total == 0) {
            return NULL;
        }
        p[0] = 0x80; p[1] = 0x06; p[3] = 0x02;
        p[6] = s->vhost_cfg_total & 0xFF;
        p[7] = s->vhost_cfg_total >> 8;
        return p;
    case 5: /* GET_DESCRIPTOR string 0 */
        p[0] = 0x80; p[1] = 0x06; p[3] = 0x03;
        p[4] = 0x09; p[5] = 0x04; /* langid 0x0409 */
        p[6] = 255;
        return p;
    case 6: /* GET_DESCRIPTOR iManufacturer */
        idx = s->vhost_dev_iman;
        break;
    case 7: /* GET_DESCRIPTOR iProduct */
        idx = s->vhost_dev_iprod;
        break;
    case 8: /* GET_DESCRIPTOR iSerialNumber */
        idx = s->vhost_dev_iser;
        break;
    case 9: /* SET_CONFIGURATION 1 */
        p[1] = 0x09; p[2] = 0x01;
        return p;
    default:
        return NULL;
    }

    if (idx == 0) {
        /* no such string: skip to the next sequence step */
        s->vhost_seq = seq + 1;
        return vhost_make_setup(s, s->vhost_seq);
    }
    p[0] = 0x80; p[1] = 0x06; p[2] = idx; p[3] = 0x03;
    p[4] = 0x09; p[5] = 0x04;
    p[6] = 255;
    return p;
}

static void vhost_send_setup(Esp32s3UsbOtgState *s)
{
    const uint8_t *setup;

    setup = vhost_make_setup(s, s->vhost_seq);
    if (setup == NULL) {
        s->vhost = VHOST_CONFIGURED;
        DWC2_DEBUG("dwc2: vhost: configured\n");
        return;
    }

    /* SETUP_RX status word + 8 data bytes, then SETUP_DONE */
    dwc2_rxstsp_push(s, 0, 8, PKTSTS_SETUP_RX);
    fifo8_push_all(&s->grx_fifo, setup, 8);
    dwc2_rxstsp_push(s, 0, 0, PKTSTS_SETUP_DONE);
    s->epout[0].int_st |= DOEPINT_SETUP;

    s->vhost_wlength = setup[6] | (setup[7] << 8);
    s->vhost_in_count = 0;

    if (s->vhost_wlength == 0) {
        /* no data stage: guest answers with a status IN (ZLP) */
        s->vhost_phase = VPH_STATUS_IN_WAIT;
    } else if (setup[0] & 0x80) {
        s->vhost_phase = VPH_DATA_IN;
    } else {
        s->vhost_phase = VPH_DATA_OUT;
    }

    DWC2_DEBUG("dwc2: vhost: SETUP seq=%u %02x %02x wLength=%u\n",
               s->vhost_seq, setup[0], setup[1], s->vhost_wlength);
    dwc2_update_irq(s);
}

/* A control transfer finished; advance to the next enumeration step. */
static void vhost_ctrl_done(Esp32s3UsbOtgState *s)
{
    s->vhost_seq++;
    s->vhost_stalled = false;
    s->vhost_phase = VPH_SETUP_SENT;
    s->vhost_deadline_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                           + (int64_t)VHOST_STEP_TIMEOUT_MS * SCALE_MS;
    vhost_send_setup(s);
}

/* Guest pushed `n` bytes of IN data on EP0. */
static void vhost_on_ep0_in(Esp32s3UsbOtgState *s, unsigned n)
{
    if (s->vhost != VHOST_CTRL) {
        return;
    }

    if (s->vhost_phase == VPH_STATUS_IN_WAIT) {
        /* zero-length status IN completing a control write (incl. a
         * no-data-stage write like SET_ADDRESS / SET_CONFIGURATION) */
        DWC2_DEBUG("dwc2: vhost: control write complete\n");
        vhost_ctrl_done(s);
        return;
    }

    if (s->vhost_phase == VPH_SETUP_SENT || s->vhost_phase == VPH_DATA_IN) {
        s->vhost_phase = VPH_DATA_IN;
        /* data stage ends on a short packet or when wLength is reached */
        if (n < EP0_MPS || s->vhost_in_count >= s->vhost_wlength) {
            s->vhost_phase = VPH_STATUS_OUT_WAIT;
            DWC2_DEBUG("dwc2: vhost: data-in done (%u bytes)\n",
                       s->vhost_in_count);
        }
    }
}

/* Guest wrote EP0 OUT regs: data-stage arming or status-phase arming. */
static void vhost_on_ep0_out(Esp32s3UsbOtgState *s)
{
    if (s->vhost != VHOST_CTRL) {
        return;
    }

    if (tsiz_xfer_size(s->epout[0].tsiz) == 0) {
        if (s->vhost_phase == VPH_STATUS_OUT_WAIT) {
            /* status ZLP for a control read: deliver + complete */
            DWC2_DEBUG("dwc2: vhost: control read complete\n");
            dwc2_rxstsp_push(s, 0, 0, PKTSTS_RX_DATA);
            dwc2_rxstsp_push(s, 0, 0, PKTSTS_RX_COMPLETE);
            s->epout[0].int_st |= DOEPINT_XFRC;
            dwc2_update_irq(s);
            vhost_ctrl_done(s);
        }
        return;
    }

    if (s->vhost_phase == VPH_DATA_OUT) {
        /* data stage of a control write: deliver zeros (no real payload) */
        uint32_t n = tsiz_xfer_size(s->epout[0].tsiz);
        uint8_t zeros[64];

        while (n > 0) {
            uint32_t pkt = MIN(n, EP0_MPS);
            memset(zeros, 0, pkt);
            dwc2_rx_fifo_push(s, zeros, pkt);
            dwc2_rxstsp_push(s, 0, pkt, PKTSTS_RX_DATA);
            s->epout[0].tsiz -= pkt;
            n -= pkt;
        }
        dwc2_rxstsp_push(s, 0, 0, PKTSTS_RX_COMPLETE);
        s->epout[0].int_st |= DOEPINT_XFRC;
        s->vhost_phase = VPH_STATUS_IN_WAIT;
        dwc2_update_irq(s);
    }
}

/* Parse captured data-in bytes for enumeration bookkeeping. */
static void vhost_capture_in(Esp32s3UsbOtgState *s)
{
    if (s->vhost != VHOST_CTRL || s->vhost_in_count == 0) {
        return;
    }

    switch (s->vhost_seq) {
    case 2: /* device descriptor */
        if (s->vhost_in_count >= 17) {
            s->vhost_dev_iman = s->vhost_in_buf[14];
            s->vhost_dev_iprod = s->vhost_in_buf[15];
            s->vhost_dev_iser = s->vhost_in_buf[16];
        }
        break;
    case 3: /* 9-byte config descriptor prefix */
        if (s->vhost_in_count >= 4) {
            s->vhost_cfg_total = s->vhost_in_buf[2] | (s->vhost_in_buf[3] << 8);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Virtual host state machine                                          */
/* ------------------------------------------------------------------ */

static void vhost_process(Esp32s3UsbOtgState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    switch (s->vhost) {
    case VHOST_DISCONNECTED:
        break;

    case VHOST_WAIT_RESET:
        if (now >= s->vhost_deadline_ns) {
            DWC2_DEBUG("dwc2: vhost: bus reset\n");
            /* USBRST + ENUMDNE together: dcd_int_handler processes both
             * in one pass (reset handling first, then enumeration done). */
            s->gintsts |= GINT_B_USBRST | GINT_B_ENUMDNE;
            s->vhost = VHOST_WAIT_ENUM;
            s->vhost_deadline_ns = now + (int64_t)VHOST_ENUM_DELAY_MS * SCALE_MS;
            dwc2_update_irq(s);
        }
        break;

    case VHOST_WAIT_ENUM:
        if (now >= s->vhost_deadline_ns) {
            s->vhost = VHOST_CTRL;
            s->vhost_seq = 0;
            s->vhost_phase = VPH_SETUP_SENT;
            s->vhost_deadline_ns = now + (int64_t)VHOST_STEP_TIMEOUT_MS * SCALE_MS;
            vhost_send_setup(s);
        }
        break;

    case VHOST_CTRL:
        /* timeout: the guest stalled or never answered; move on */
        if (now >= s->vhost_deadline_ns) {
            DWC2_DEBUG("dwc2: vhost: control step timeout, advancing\n");
            vhost_ctrl_done(s);
        }
        break;

    case VHOST_CONFIGURED:
        break;
    }
}

static void vhost_connect(Esp32s3UsbOtgState *s)
{
    if (s->vhost != VHOST_DISCONNECTED) {
        return;
    }
    s->vhost = VHOST_WAIT_RESET;
    s->vhost_deadline_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                           + (int64_t)VHOST_RESET_DELAY_MS * SCALE_MS;
}

static void vhost_disconnect(Esp32s3UsbOtgState *s)
{
    s->vhost = VHOST_DISCONNECTED;
    s->gintsts &= ~(GINT_B_USBRST | GINT_B_ENUMDNE);
}

static void dwc2_frame_tick(void *opaque)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(opaque);
    static unsigned dump_div;

    timer_mod(&s->frame_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + VHOST_FRAME_PERIOD_MS);

    if (s->gintmsk & GINT_B_SOF) {
        s->gintsts |= GINT_B_SOF;
        dwc2_update_irq(s);
    }

    if (++dump_div >= 500) {
        dump_div = 0;
        DWC2_DEBUG("dwc2: regs vhost=%d gintsts=%08x gintmsk=%08x gahbcfg=%08x "
                   "daintmsk=%08x diepmsk=%08x doepmsk=%08x diepempmsk=%08x dctl=%08x\n",
                   s->vhost, dwc2_gintsts_effective(s), s->gintmsk, s->gahbcfg,
                   s->daintmsk, s->diepmsk, s->doepmsk, s->diepempmsk, s->dctl);
    }

    vhost_process(s);
}

/* IN (guest -> host) FIFO handling */
/* Guest pushed 4 bytes (one FIFO access) into IN FIFO `ep`. */
static void dwc2_in_fifo_write(Esp32s3UsbOtgState *s, unsigned ep, uint32_t value)
{
    uint8_t b[4];
    uint32_t left = s->epin[ep].xfer_left;
    unsigned real;
    unsigned captured = 0;

    if (left == 0) {
        return; /* transfer not armed; drop */
    }

    b[0] = value & 0xFF;
    b[1] = (value >> 8) & 0xFF;
    b[2] = (value >> 16) & 0xFF;
    b[3] = (value >> 24) & 0xFF;

    real = MIN(4u, left);
    fifo8_push_all(&s->epin[ep].data, b, real);
    s->epin[ep].xfer_left -= real;
    /* hardware decrements DIEPTSIZ as bytes are pushed */
    s->epin[ep].tsiz -= real;

    /*
     * Transmit whatever is queued as soon as the driver finishes pushing
     * a packet. The driver only ever writes whole packets (it checks
     * DTXFSTS free space for the full packet before pushing), so a
     * non-empty FIFO after a burst with xfer_left > 0 is exactly one
     * complete packet that a real core would put on the wire
     * immediately; draining it lets the driver push the next packet via
     * TXFE. Holding data until xfer_left == 0 deadlocks multi-packet
     * transfers (the FIFO fills and the driver can never continue).
     */
    while (!fifo8_is_empty(&s->epin[ep].data)) {
        uint32_t navail = 0;
        const uint8_t *src = fifo8_peek_bufptr(&s->epin[ep].data,
                                               fifo8_num_used(&s->epin[ep].data),
                                               &navail);
        DWC2_DEBUG("dwc2: IN ep%u %u bytes\n", ep, navail);
        fifo8_push_all(&s->tx_ch_fifo, src, navail);
        fifo8_drop(&s->epin[ep].data, navail);
        if (ep == 0 && s->vhost_in_count < sizeof(s->vhost_in_buf)) {
            unsigned cap = sizeof(s->vhost_in_buf) - s->vhost_in_count;
            unsigned cpy = MIN((unsigned)navail, cap);

            memcpy(&s->vhost_in_buf[s->vhost_in_count], src, cpy);
            s->vhost_in_count += cpy;
        }
        captured += navail;
    }
    dwc2_ch_flush(s);

    if (s->epin[ep].xfer_left == 0) {
        /* whole transfer is on the wire */
        s->epin[ep].int_st |= DIEPINT_XFRC;
        s->epin[ep].ctl &= ~EPCTL_EPENA;
        dwc2_update_irq(s);

        if (ep == 0) {
            vhost_capture_in(s);
            vhost_on_ep0_in(s, captured);
        }
    } else {
        /* more packets to come: TXFE lets the driver push the next one */
        s->epin[ep].int_st |= DIEPINT_TXFE;
        dwc2_update_irq(s);
    }
}

/* MMIO */
static uint64_t esp32s3_usb_otg_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(opaque);
    uint64_t r = 0;

    /* DFIFO windows: 0x1000 + ep * 0x1000 */
    if (addr >= R_DFIFO0 && addr < R_DFIFO_END) {
        if ((addr % R_DFIFO_STRIDE) == 0) {
            if (addr == R_DFIFO0) {
                r = dwc2_rx_fifo_pop_word(s);
                dwc2_update_irq(s);
                return r;
            }
        }
        return 0;
    }

    switch (addr) {
    case R_GOTGCTL:
        r = s->gotgctl;
        break;
    case R_GOTGINT:
        r = s->gotgint;
        break;
    case R_GAHBCFG:
        r = s->gahbcfg;
        break;
    case R_GUSBCFG:
        r = s->gusbcfg;
        break;
    case R_GRSTCTL:
        r = s->grstctl | GRSTCTL_AHBIDL;
        break;
    case R_GINTSTS:
        r = dwc2_gintsts_effective(s);
        break;
    case R_GINTMSK:
        r = s->gintmsk;
        break;
    case R_GRXSTSR:
        r = dwc2_rxstsp_peek(s);
        break;
    case R_GRXSTSP: {
        uint32_t v = dwc2_rxstsp_peek(s);
        DWC2_DEBUG("dwc2: GRXSTSP pop ep%u bcnt=%u pktsts=%u\n",
                   v & 0xF, (v >> 4) & 0x7FF, (v >> 17) & 0xF);
        dwc2_rxstsp_pop(s);
        r = v;
        dwc2_update_irq(s);
        break;
    }
    case R_GRXFSIZ:
        r = s->grxfsiz;
        break;
    case R_DIEPTXF0:
        r = s->dieptxf[0];
        break;
    case R_GNPTXSTS:
        /* EP0 tx FIFO free space (words); request queue not modeled */
        r = (s->dieptxf[0] >> 16) & 0xFFFFu;
        break;
    case R_GSNPSID:
        r = GSNPSID_VAL;
        break;
    case R_GHWCFG1:
        r = GHWCFG1_VAL;
        break;
    case R_GHWCFG2:
        r = GHWCFG2_VAL;
        break;
    case R_GHWCFG3:
        r = GHWCFG3_VAL;
        break;
    case R_GHWCFG4:
        r = GHWCFG4_VAL;
        break;
    case R_GDFIFOCFG:
        r = s->gdfifocfg;
        break;
    case R_DCFG:
        r = s->dcfg;
        break;
    case R_DCTL:
        r = s->dctl;
        break;
    case R_DSTS:
        /* FS on dedicated 48 MHz PHY (enum speed 3), not suspended */
        r = (3u << 1);
        break;
    case R_DIEPMSK:
        r = s->diepmsk;
        break;
    case R_DOEPMSK:
        r = s->doepmsk;
        break;
    case R_DAINT:
        r = dwc2_daint_derived(s);
        break;
    case R_DAINTMSK:
        r = s->daintmsk;
        break;
    case R_DIEPEMPMSK:
        r = s->diepempmsk;
        break;
    case R_PCGCCTL:
        r = s->pcgcctl;
        break;
    default:
        if (addr >= R_EPIN_BASE
            && addr < R_EPIN_BASE + EP_BLK_SIZE * ESP32S3_USB_EP_COUNT) {
            unsigned ep = (addr - R_EPIN_BASE) / EP_BLK_SIZE;
            unsigned off = (addr - R_EPIN_BASE) % EP_BLK_SIZE;
            switch (off) {
            case EP_OFF_CTL:
                r = s->epin[ep].ctl;
                break;
            case EP_OFF_INT:
                r = s->epin[ep].int_st;
                break;
            case EP_OFF_TSIZ:
                r = s->epin[ep].tsiz;
                break;
            case EP_OFF_DTXFSTS: {
                uint32_t used = (fifo8_num_used(&s->epin[ep].data) + 3u) / 4u;
                r = (s->epin[ep].txf_words > used)
                    ? (s->epin[ep].txf_words - used) : 0;
                break;
            }
            default:
                r = 0;
                break;
            }
        } else if (addr >= R_EPOUT_BASE
                   && addr < R_EPOUT_BASE + EP_BLK_SIZE * ESP32S3_USB_EP_COUNT) {
            unsigned ep = (addr - R_EPOUT_BASE) / EP_BLK_SIZE;
            unsigned off = (addr - R_EPOUT_BASE) % EP_BLK_SIZE;
            switch (off) {
            case EP_OFF_CTL:
                r = s->epout[ep].ctl;
                break;
            case EP_OFF_INT:
                r = s->epout[ep].int_st;
                break;
            case EP_OFF_TSIZ:
                r = s->epout[ep].tsiz;
                break;
            default:
                r = 0;
                break;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "dwc2: read from unimplemented offset 0x%04"
                          HWADDR_PRIX "\n", addr);
            r = 0;
        }
        break;
    }

    return r;
}

static void esp32s3_usb_otg_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned int size)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(opaque);
    bool pump_out = false;

    /* DFIFO windows */
    if (addr >= R_DFIFO0 && addr < R_DFIFO_END) {
        if ((addr % R_DFIFO_STRIDE) == 0) {
            unsigned ep = (addr - R_DFIFO0) / R_DFIFO_STRIDE;
            if (ep < ESP32S3_USB_EP_COUNT) {
                dwc2_in_fifo_write(s, ep, value);
            }
        }
        return;
    }

    switch (addr) {
    case R_GOTGCTL:
        s->gotgctl = value;
        break;
    case R_GOTGINT:
        s->gotgint &= ~value; /* w1c */
        break;
    case R_GAHBCFG:
        s->gahbcfg = value;
        dwc2_update_irq(s);
        break;
    case R_GUSBCFG:
        s->gusbcfg = value;
        break;
    case R_GRSTCTL:
        if (value & GRSTCTL_CSRST) {
            /* self-clearing core soft reset: drop volatile state */
            unsigned i;
            for (i = 0; i < ESP32S3_USB_EP_COUNT; i++) {
                fifo8_reset(&s->epin[i].data);
                s->epin[i].xfer_left = 0;
                s->epin[i].int_st = 0;
                s->epin[i].tsiz = 0;
                s->epout[i].int_st = 0;
                s->epout[i].tsiz = 0;
            }
            fifo8_reset(&s->grx_fifo);
            s->rxstsp_head = s->rxstsp_tail = 0;
            s->gintsts = 0;
            s->grstctl = 0;
        } else {
            s->grstctl = value & ~(GRSTCTL_RXFFLSH | GRSTCTL_TXFFLSH);
        }
        if (value & GRSTCTL_RXFFLSH) {
            fifo8_reset(&s->grx_fifo);
            s->rxstsp_head = s->rxstsp_tail = 0;
        }
        if (value & GRSTCTL_TXFFLSH) {
            unsigned fnum = (value & GRSTCTL_TXFNUM_MASK) >> GRSTCTL_TXFNUM_SHIFT;
            unsigned i;
            for (i = 0; i < ESP32S3_USB_EP_COUNT; i++) {
                if (fnum == 0x10 || fnum == i) {
                    fifo8_reset(&s->epin[i].data);
                }
            }
        }
        dwc2_update_irq(s);
        break;
    case R_GINTSTS:
        DWC2_DEBUG("dwc2: GINTSTS w1c write %08x (stored=%08x)\n",
                   (unsigned)value, s->gintsts);
        s->gintsts &= ~value; /* w1c (derived bits ignored) */
        dwc2_update_irq(s);
        break;
    case R_GINTMSK:
        s->gintmsk = value;
        dwc2_update_irq(s);
        break;
    case R_GRXFSIZ:
        s->grxfsiz = value & 0xFFFFu;
        break;
    case R_DIEPTXF0:
        s->dieptxf[0] = value;
        s->epin[0].txf_words = (value >> 16) & 0xFFFFu;
        break;
    case R_DCFG:
        s->dcfg = value;
        break;
    case R_DCTL: {
        uint32_t old = s->dctl;
        s->dctl = value;
        if ((old & DCTL_SDIS) && !(value & DCTL_SDIS)) {
            /* soft connect: the virtual host starts the bus */
            DWC2_DEBUG("dwc2: soft connect\n");
            vhost_connect(s);
        } else if (!(old & DCTL_SDIS) && (value & DCTL_SDIS)) {
            DWC2_DEBUG("dwc2: soft disconnect\n");
            vhost_disconnect(s);
        }
        if (value & DCTL_SGONAK) {
            s->gintsts |= GINT_B_GOUTNAKEFF;
        }
        if (value & DCTL_CGONAK) {
            s->gintsts &= ~GINT_B_GOUTNAKEFF;
        }
        dwc2_update_irq(s);
        break;
    }
    case R_DIEPMSK:
        s->diepmsk = value;
        DWC2_DEBUG("dwc2: DIEPMSK = %08x\n", (unsigned)value);
        dwc2_update_irq(s);
        break;
    case R_DOEPMSK:
        s->doepmsk = value;
        dwc2_update_irq(s);
        break;
    case R_DAINTMSK:
        s->daintmsk = value;
        dwc2_update_irq(s);
        break;
    case R_DIEPEMPMSK:
        s->diepempmsk = value;
        DWC2_DEBUG("dwc2: DIEPEMPMSK = %08x\n", (unsigned)value);
        dwc2_update_irq(s);
        break;
    case R_PCGCCTL:
        s->pcgcctl = value;
        break;
    default:
        if (addr == R_DIEPTXF0_N) {
            /* handled below via the range check */
        }
        if (addr >= R_DIEPTXF0_N && addr <= R_DIEPTXF_LAST
            && ((addr - R_DIEPTXF0_N) % 4) == 0) {
            unsigned n = (addr - R_DIEPTXF0_N) / 4;
            unsigned ep = n + 1;

            s->dieptxf[n + 1] = value;
            if (ep < ESP32S3_USB_EP_COUNT) {
                s->epin[ep].txf_words = (value >> 16) & 0xFFFFu;
            }
            break;
        }
        if (addr >= R_EPIN_BASE
            && addr < R_EPIN_BASE + EP_BLK_SIZE * ESP32S3_USB_EP_COUNT) {
            unsigned ep = (addr - R_EPIN_BASE) / EP_BLK_SIZE;
            unsigned off = (addr - R_EPIN_BASE) % EP_BLK_SIZE;
            switch (off) {
            case EP_OFF_CTL:
                s->epin[ep].ctl = value;
                if (ep == 0) {
                    DWC2_DEBUG("dwc2: DIEPCTL0=%08x (tsiz=%u left=%u)\n",
                               (unsigned)value, tsiz_xfer_size(s->epin[0].tsiz),
                               s->epin[0].xfer_left);
                } else {
                    DWC2_DEBUG("dwc2: DIEPCTL%u=%08x (tsiz=%u left=%u)\n",
                               ep, (unsigned)value, tsiz_xfer_size(s->epin[ep].tsiz),
                               s->epin[ep].xfer_left);
                }
                if (value & EPCTL_EPDIS) {
                    /* self-complete the disable */
                    s->epin[ep].int_st |= DIEPINT_EPDISD;
                    s->epin[ep].ctl &= ~EPCTL_EPENA;
                }
                if (value & EPCTL_EPENA) {
                    if (tsiz_xfer_size(s->epin[ep].tsiz) == 0) {
                        /* zero-length IN (e.g. control status): complete now */
                        s->epin[ep].int_st |= DIEPINT_XFRC;
                        s->epin[ep].ctl &= ~EPCTL_EPENA;
                        dwc2_update_irq(s);
                        if (ep == 0) {
                            vhost_on_ep0_in(s, 0);
                        }
                    } else {
                        /*
                         * Real silicon: a freshly-armed TxFIFO starts out
                         * empty, so TXFE fires immediately (same condition
                         * as "FIFO drained below threshold" mid-transfer,
                         * handled below in the DFIFO-write path). Drivers
                         * that are TXFE-interrupt-driven for the push itself
                         * (not just continuation packets) need this initial
                         * kick to ever write the first word — without it the
                         * transfer stays armed forever with no data on the
                         * wire. Mirrors the "more packets to come" TXFE
                         * assert below.
                         */
                        s->epin[ep].int_st |= DIEPINT_TXFE;
                        dwc2_update_irq(s);
                    }
                }
                break;
            case EP_OFF_INT:
                /* w1c, except TXFE which is read-only */
                s->epin[ep].int_st &= ~(value & ~DIEPINT_TXFE);
                dwc2_update_irq(s);
                break;
            case EP_OFF_TSIZ:
                s->epin[ep].tsiz = value;
                s->epin[ep].xfer_left = tsiz_xfer_size(value);
                if (ep == 0) {
                    DWC2_DEBUG("dwc2: DIEPTSIZ0=%08x (left=%u)\n",
                               (unsigned)value, s->epin[0].xfer_left);
                }
                break;
            default:
                break;
            }
        } else if (addr >= R_EPOUT_BASE
                   && addr < R_EPOUT_BASE + EP_BLK_SIZE * ESP32S3_USB_EP_COUNT) {
            unsigned ep = (addr - R_EPOUT_BASE) / EP_BLK_SIZE;
            unsigned off = (addr - R_EPOUT_BASE) % EP_BLK_SIZE;
            switch (off) {
            case EP_OFF_CTL:
                s->epout[ep].ctl = value;
                if (ep != 0) {
                    DWC2_DEBUG("dwc2: DOEPCTL%u=%08x (tsiz=%u pend=%u)\n",
                               ep, (unsigned)value,
                               tsiz_xfer_size(s->epout[ep].tsiz),
                               fifo8_num_used(&s->rx_pending));
                }
                if (value & EPCTL_EPDIS) {
                    s->epout[ep].int_st |= DOEPINT_EPDISD;
                    s->epout[ep].ctl &= ~EPCTL_EPENA;
                }
                dwc2_update_irq(s);
                if (ep == 0) {
                    vhost_on_ep0_out(s);
                }
                pump_out = true;
                break;
            case EP_OFF_INT:
                DWC2_DEBUG("dwc2: DOEPINT%u w1c %08x\n", ep, (unsigned)value);
                s->epout[ep].int_st &= ~value; /* w1c */
                dwc2_update_irq(s);
                break;
            case EP_OFF_TSIZ:
                DWC2_DEBUG("dwc2: DOEPTSIZ%u = %08x\n", ep, (unsigned)value);
                s->epout[ep].tsiz = value;
                if (ep == 0) {
                    vhost_on_ep0_out(s);
                }
                break;
            default:
                break;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "dwc2: write to unimplemented offset 0x%04"
                          HWADDR_PRIX " = 0x%08" PRIx64 "\n",
                          (unsigned)addr, value);
        }
        break;
    }

    if (pump_out) {
        dwc2_out_pump(s);
    }
}

/* QOM boilerplate */
static const MemoryRegionOps esp32s3_usb_otg_ops = {
    .read = esp32s3_usb_otg_read,
    .write = esp32s3_usb_otg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void esp32s3_usb_otg_reset_hold(Object *obj, ResetType type)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(obj);
    unsigned i;

    s->gotgctl = 0;
    s->gotgint = 0;
    s->gahbcfg = 0;
    s->gusbcfg = 0;
    s->grstctl = 0;
    s->gintsts = 0;
    s->gintmsk = 0;
    s->grxfsiz = 0;
    s->gdfifocfg = 0;
    s->dcfg = 0;
    s->dctl = DCTL_SDIS; /* disconnected until the guest soft-connects */
    s->diepmsk = 0;
    s->doepmsk = 0;
    s->daintmsk = 0;
    s->diepempmsk = 0;
    s->pcgcctl = 0;

    for (i = 0; i < ESP32S3_USB_EP_COUNT; i++) {
        fifo8_reset(&s->epin[i].data);
        s->epin[i].ctl = 0;
        s->epin[i].int_st = 0;
        s->epin[i].tsiz = 0;
        s->epin[i].txf_words = 0;
        s->epin[i].xfer_left = 0;
        s->epout[i].ctl = 0;
        s->epout[i].int_st = 0;
        s->epout[i].tsiz = 0;
    }
    fifo8_reset(&s->grx_fifo);
    fifo8_reset(&s->rx_pending);
    fifo8_reset(&s->tx_ch_fifo);
    s->rxstsp_head = s->rxstsp_tail = 0;

    s->vhost = VHOST_DISCONNECTED;
    s->vhost_phase = VPH_SETUP_SENT;
    s->vhost_seq = 0;
    s->vhost_wlength = 0;
    s->vhost_in_count = 0;
    s->vhost_cfg_total = 0;
    s->vhost_stalled = false;
    s->vhost_dev_iman = 0;
    s->vhost_dev_iprod = 0;
    s->vhost_dev_iser = 0;

    qemu_set_irq(s->irq, 0);
}

static void esp32s3_usb_otg_realize(DeviceState *dev, Error **errp)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(dev);

    qemu_chr_fe_set_handlers(&s->chr, dwc2_ch_can_receive, dwc2_ch_receive,
                             dwc2_ch_event, NULL, s, NULL, true);

    /* arm the 1 ms host/frame tick */
    timer_mod(&s->frame_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + VHOST_FRAME_PERIOD_MS);
}

static void esp32s3_usb_otg_init(Object *obj)
{
    Esp32s3UsbOtgState *s = ESP32S3_USB_OTG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned i;

    memory_region_init_io(&s->iomem, obj, &esp32s3_usb_otg_ops, s,
                          TYPE_ESP32S3_USB_OTG, ESP32S3_USB_OTG_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    for (i = 0; i < ESP32S3_USB_EP_COUNT; i++) {
        fifo8_create(&s->epin[i].data, 2048);
    }
    fifo8_create(&s->grx_fifo, 2048);
    fifo8_create(&s->rx_pending, 16384);
    fifo8_create(&s->tx_ch_fifo, 16384);

    timer_init_ms(&s->frame_timer, QEMU_CLOCK_VIRTUAL, dwc2_frame_tick, s);
}

static Property esp32s3_usb_otg_properties[] = {
    DEFINE_PROP_CHR("chardev", Esp32s3UsbOtgState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_usb_otg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32s3_usb_otg_reset_hold;
    dc->realize = esp32s3_usb_otg_realize;
    device_class_set_props(dc, esp32s3_usb_otg_properties);
    dc->desc = "ESP32-S3 USB-OTG (DWC2 device mode)";
}

static const TypeInfo esp32s3_usb_otg_info = {
    .name = TYPE_ESP32S3_USB_OTG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3UsbOtgState),
    .instance_init = esp32s3_usb_otg_init,
    .class_init = esp32s3_usb_otg_class_init,
};

static void esp32s3_usb_otg_register_types(void)
{
    type_register_static(&esp32s3_usb_otg_info);
}

type_init(esp32s3_usb_otg_register_types)

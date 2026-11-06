/*
 * ESP32-S3 USB-OTG (Synopsys DWC2, device mode) emulation
 *
 * Emulates the DWC2 FS core found in the ESP32-S3 (base 0x60080000) in
 * device (peripheral) mode only, slave/FIFO access (the only mode the
 * ESP32-S3 core supports; TinyUSB's dcd_dwc2 driver runs it in slave mode).
 *
 * The "other side" of the USB link is a synthetic USB host implemented
 * inside the device: after the guest soft-connects (DCTL.SFTDISCON), it
 * performs a scripted enumeration (descriptor requests parsed from the
 * guest's own responses) and then exchanges bulk data with a QEMU chardev
 * backend (host-side socket/pipe). This makes the guest's USB device stack
 * (e.g. TinyUSB vendor/CDC class) fully exercisable without a real USB bus.
 */
#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qemu/fifo8.h"
#include "qemu/timer.h"

#define TYPE_ESP32S3_USB_OTG "usb.esp32s3.otg"
#define ESP32S3_USB_OTG(obj) OBJECT_CHECK(Esp32s3UsbOtgState, (obj), TYPE_ESP32S3_USB_OTG)

/* Register block size: covers CSR area + the 16 DFIFO windows (0x1000..0xFFFF). */
#define ESP32S3_USB_OTG_REGS_SIZE 0x10000

/* ESP32-S3 DWC2: 6 device endpoints + EP0 (GHWCFG2.num_dev_ep = 6). */
#define ESP32S3_USB_EP_COUNT 7

/* GRXSTSP status queue depth (power of two not required; modulo-based) */
#define ESP32S3_USB_RXSTSP_QLEN 16

/* Virtual host state */
typedef enum {
    VHOST_DISCONNECTED,   /* DCTL.SFTDISCON set (or never connected) */
    VHOST_WAIT_RESET,     /* connected; timer will raise USBRST */
    VHOST_WAIT_ENUM,      /* USBRST/ENUMDNE raised; timer will start enumeration */
    VHOST_CTRL,           /* a control transfer is in flight */
    VHOST_CONFIGURED,     /* SET_CONFIGURATION done; bulk data flowing */
} Esp32s3VhostState;

/* Control-transfer phase inside VHOST_CTRL */
typedef enum {
    VPH_SETUP_SENT,       /* SETUP delivered, waiting for guest reaction */
    VPH_DATA_IN,          /* control read: guest pushes descriptor data */
    VPH_DATA_OUT,         /* control write: guest armed EP0 OUT for data */
    VPH_STATUS_OUT_WAIT,  /* control read: waiting for guest to arm EP0 OUT status */
    VPH_STATUS_IN_WAIT,   /* control write: waiting for guest to arm EP0 IN status */
} Esp32s3VhostPhase;

typedef struct Esp32s3UsbOtgState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    CharBackend chr;

    /* 1 ms virtual-time tick: SOF + frame counter + host timeouts */
    QEMUTimer frame_timer;

    /* ---- register mirror ---- */
    uint32_t gotgctl;
    uint32_t gotgint;
    uint32_t gahbcfg;
    uint32_t gusbcfg;
    uint32_t grstctl;
    uint32_t gintsts;    /* w1c-able portion; RXFLVL/IEPINT/OEPINT derived */
    uint32_t gintmsk;
    uint32_t grxfsiz;
    uint32_t dieptxf[16]; /* [0] = DIEPTXF0; [n] = DIEPTXFn (IN EP n) */
    uint32_t gdfifocfg;
    uint32_t dcfg;
    uint32_t dctl;
    uint32_t diepmsk;
    uint32_t doepmsk;
    uint32_t daintmsk;
    uint32_t diepempmsk;
    uint32_t pcgcctl;

    /* IN endpoints (FIFO mode) */
    struct {
        uint32_t ctl;       /* DIEPCTLn */
        uint32_t int_st;    /* DIEPINTn (w1c) */
        uint32_t tsiz;      /* DIEPTSIZn */
        uint32_t txf_words; /* TX FIFO capacity (from DIEPTXFn), 0 = not allocated */
        uint32_t xfer_left; /* bytes of the armed transfer not yet pushed */
        Fifo8 data;         /* packet bytes queued for the virtual host */
    } epin[ESP32S3_USB_EP_COUNT];

    /* OUT endpoints */
    struct {
        uint32_t ctl;       /* DOEPCTLn */
        uint32_t int_st;    /* DOEPINTn (w1c) */
        uint32_t tsiz;      /* DOEPTSIZn */
    } epout[ESP32S3_USB_EP_COUNT];

    /* ---- virtual USB host ---- */
    Esp32s3VhostState vhost;
    Esp32s3VhostPhase vhost_phase;
    int vhost_seq;              /* enumeration sequence index */
    int64_t vhost_deadline_ns;  /* virtual-clock deadline for current state */
    uint8_t vhost_setup[8];     /* SETUP packet being transferred */
    uint16_t vhost_wlength;     /* data-stage length of current control xfer */
    uint32_t vhost_in_count;    /* data-in bytes delivered so far */
    uint8_t vhost_in_buf[512];  /* captured data-in bytes (desc parsing) */
    uint16_t vhost_cfg_total;   /* wTotalLength parsed from config desc */
    uint8_t vhost_dev_iman;     /* parsed string indexes from device desc */
    uint8_t vhost_dev_iprod;
    uint8_t vhost_dev_iser;
    bool vhost_stalled;         /* guest STALLed EP0 -> skip current request */

    /* OUT data path: bytes received from chardev, awaiting an armed OUT EP */
    Fifo8 rx_pending;
    Fifo8 grx_fifo;             /* shared RX data FIFO (word-aligned reads) */

    /* GRXSTSP receive-status queue (circular buffer of 32-bit words) */
    uint32_t rxstsp_q[ESP32S3_USB_RXSTSP_QLEN];
    uint32_t rxstsp_head;
    uint32_t rxstsp_tail;

    /* IN data path: bytes waiting for the chardev to accept */
    Fifo8 tx_ch_fifo;
    guint tx_watch;
} Esp32s3UsbOtgState;

/*
 * ESP32-C3 I2S controller emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * The ESP-IDF I2S driver pushes audio buffers to the I2S peripheral through the
 * shared GDMA controller and then blocks in i2s_channel_write() until the GDMA
 * OUT_EOF interrupt signals that a buffer has been consumed. With no I2S hardware
 * present, that interrupt never fires and the firmware hangs. The HAL also spins
 * on the TX_UPDATE / RX_UPDATE register sync bits.
 *
 * This model fixes both:
 *   - TX_UPDATE / RX_UPDATE are auto-cleared, so i2s_ll_tx_update() returns.
 *   - When TX is started, a periodic timer drains one DMA buffer per tick via the
 *     existing esp_gdma_read_channel() (the same "pull" mechanism the SHA
 *     accelerator uses). That call raises OUT_EOF on the GDMA channel, which is
 *     exactly the interrupt the I2S driver's DMA callback waits on.
 *
 * Each drained PCM buffer is also forwarded to an optional chardev backend so the
 * audio can be captured/streamed (e.g. over a websocket socket chardev).
 *
 * Pacing tracks real time: the data byte-rate is derived from the TX clock-divider
 * registers so a buffer of N bytes takes ~N/byte_rate seconds to drain.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/dma/esp_gdma.h"
#include "hw/audio/esp32c3_i2s.h"

#define I2S_WARNING 0
#define I2S_DEBUG   0

/* Mask of the interrupt bits we model (RX_DONE, TX_DONE, RX_HUNG, TX_HUNG). */
#define ESP32C3_I2S_INT_MASK    0x0F

/* Fallback tick period when the clock registers are not yet meaningfully
 * configured (1 ms of virtual time). Keeps the drain loop alive without busy
 * spinning. */
#define ESP32C3_I2S_FALLBACK_NS (1 * 1000 * 1000)


static inline uint32_t *i2s_reg(Esp32C3I2SState *s, hwaddr addr)
{
    return &s->regs[addr / sizeof(uint32_t)];
}

/* Recompute the device IRQ level from INT_RAW & INT_ENA. */
static void esp32c3_i2s_update_irq(Esp32C3I2SState *s)
{
    const uint32_t st = s->regs[R_I2S_INT_RAW] & s->regs[R_I2S_INT_ENA] &
                        ESP32C3_I2S_INT_MASK;
    qemu_set_irq(s->irq, st ? 1 : 0);
}

/*
 * Derive the TX data byte-rate (bytes/second the FIFO drains) from the clock
 * registers. byte_rate = bclk / 8, where bclk = mclk / bck_div and
 * mclk = src_clk / clkm_div. For software-mono the DMA buffer carries a single
 * channel that hardware duplicates across both slots, so the data rate is halved.
 *
 * The fractional part of the MCLK divider (x/y/z) is intentionally ignored: the
 * integer divider already places pacing within ~1% of the true 44.1/48 kHz rate,
 * and audio data correctness does not depend on pacing at all.
 */
static uint64_t esp32c3_i2s_tx_byte_rate(Esp32C3I2SState *s)
{
    const uint32_t clkm  = s->regs[R_I2S_TX_CLKM_CONF];
    const uint32_t conf1 = s->regs[R_I2S_TX_CONF1];
    const uint32_t conf  = s->regs[R_I2S_TX_CONF];

    uint64_t src_hz;
    switch (FIELD_EX32(clkm, I2S_TX_CLKM_CONF, TX_CLK_SEL)) {
    case 0:  src_hz = 40000000;  break;  /* XTAL */
    case 2:  src_hz = 160000000; break;  /* PLL_160M */
    default: src_hz = 160000000; break;  /* external/unknown: assume PLL */
    }

    uint32_t clkm_div = FIELD_EX32(clkm, I2S_TX_CLKM_CONF, TX_CLKM_DIV_NUM);
    uint32_t bck_div  = FIELD_EX32(conf1, I2S_TX_CONF1, TX_BCK_DIV_NUM);
    if (clkm_div == 0 || bck_div == 0) {
        return 0;
    }

    uint64_t mclk = src_hz / clkm_div;
    uint64_t bclk = mclk / bck_div;
    uint64_t byte_rate = bclk / 8;

    if (FIELD_EX32(conf, I2S_TX_CONF, TX_MONO)) {
        byte_rate /= 2;
    }

    return byte_rate;
}

/* Schedule the next drain tick, pacing it to the real-time duration of `len` bytes. */
static void esp32c3_i2s_schedule(Esp32C3I2SState *s, uint32_t len)
{
    int64_t delay_ns = ESP32C3_I2S_FALLBACK_NS;
    const uint64_t byte_rate = esp32c3_i2s_tx_byte_rate(s);

    if (byte_rate != 0 && len != 0) {
        delay_ns = (int64_t)(((uint64_t)len * 1000000000ULL) / byte_rate);
        if (delay_ns <= 0) {
            delay_ns = 1000; /* clamp to 1 us minimum */
        }
    }

    timer_mod(&s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay_ns);
}

/*
 * Drain exactly one DMA buffer from the GDMA OUT channel bound to I2S0. Reading
 * the channel walks the descriptor list, copies the PCM out and (when it crosses
 * a suc_eof descriptor) raises OUT_EOF on the GDMA IRQ, which unblocks the I2S
 * driver. Then forward the PCM to the chardev and reschedule.
 */
static void esp32c3_i2s_tx_tick(void *opaque)
{
    Esp32C3I2SState *s = ESP32C3_I2S(opaque);
    uint32_t chan = 0;
    uint32_t len = 0;

    if (!s->tx_running) {
        return;
    }

    /* The driver may flip tx_start before linking the GDMA channel; wait for it. */
    if (s->gdma == NULL ||
        !esp_gdma_get_channel_periph(s->gdma, GDMA_I2S0, ESP_GDMA_OUT_IDX, &chan) ||
        !esp_gdma_peek_length(s->gdma, chan, &len) || len == 0) {
        timer_mod(&s->tx_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ESP32C3_I2S_FALLBACK_NS);
        return;
    }

    g_autofree uint8_t *buf = g_malloc(len);
    if (esp_gdma_read_channel(s->gdma, chan, buf, len)) {
        if (qemu_chr_fe_backend_connected(&s->chr)) {
            /* Best-effort: drop the buffer if the sink can't take it all. */
            qemu_chr_fe_write_all(&s->chr, buf, len);
        }

        /* Reflect a completed TX in the device's own interrupt status too. */
        s->regs[R_I2S_INT_RAW] |= R_I2S_INT_RAW_TX_DONE_MASK;
        esp32c3_i2s_update_irq(s);
    } else {
#if I2S_WARNING
        warn_report("[I2S] GDMA read failed while draining TX");
#endif
    }

    esp32c3_i2s_schedule(s, len);
}

static void esp32c3_i2s_tx_set_running(Esp32C3I2SState *s, bool running)
{
    if (running == s->tx_running) {
        return;
    }

    s->tx_running = running;
    if (running) {
        /* Kick off draining as soon as possible. */
        timer_mod(&s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    } else {
        timer_del(&s->tx_timer);
    }
}


static uint64_t esp32c3_i2s_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32C3I2SState *s = ESP32C3_I2S(opaque);
    uint64_t r;

    switch (addr) {
    case A_I2S_INT_ST:
        r = s->regs[R_I2S_INT_RAW] & s->regs[R_I2S_INT_ENA] & ESP32C3_I2S_INT_MASK;
        break;
    case A_I2S_DATE:
        r = ESP32C3_I2S_DATE_VALUE;
        break;
    default:
        r = (addr + size <= ESP32C3_I2S_MEM_SIZE) ? *i2s_reg(s, addr) : 0;
        break;
    }

#if I2S_DEBUG
    info_report("[I2S] read  %04lx -> %08lx", (unsigned long)addr, (unsigned long)r);
#endif
    return r;
}

static void esp32c3_i2s_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    Esp32C3I2SState *s = ESP32C3_I2S(opaque);

    if (addr + size > ESP32C3_I2S_MEM_SIZE) {
        return;
    }

#if I2S_DEBUG
    info_report("[I2S] write %04lx <- %08lx", (unsigned long)addr, (unsigned long)value);
#endif

    switch (addr) {
    case A_I2S_TX_CONF:
        /* Auto-clear TX_UPDATE: the HAL writes it then spins until hardware
         * clears it (i2s_ll_tx_update). Store the rest verbatim. */
        s->regs[R_I2S_TX_CONF] = (uint32_t)value & ~R_I2S_TX_CONF_TX_UPDATE_MASK;
        esp32c3_i2s_tx_set_running(s,
            FIELD_EX32((uint32_t)value, I2S_TX_CONF, TX_START) != 0);
        break;

    case A_I2S_RX_CONF:
        /* Auto-clear RX_UPDATE for the same reason. RX is not otherwise modelled. */
        s->regs[R_I2S_RX_CONF] = (uint32_t)value & ~R_I2S_RX_CONF_RX_UPDATE_MASK;
        break;

    case A_I2S_INT_ENA:
        s->regs[R_I2S_INT_ENA] = (uint32_t)value;
        esp32c3_i2s_update_irq(s);
        break;

    case A_I2S_INT_CLR:
        /* Write-1-to-clear the corresponding raw bits. */
        s->regs[R_I2S_INT_RAW] &= ~((uint32_t)value & ESP32C3_I2S_INT_MASK);
        esp32c3_i2s_update_irq(s);
        break;

    case A_I2S_INT_RAW:
        s->regs[R_I2S_INT_RAW] = (uint32_t)value & ESP32C3_I2S_INT_MASK;
        esp32c3_i2s_update_irq(s);
        break;

    case A_I2S_INT_ST:
    case A_I2S_DATE:
        /* Read-only: ignore writes. */
        break;

    default:
        *i2s_reg(s, addr) = (uint32_t)value;
        break;
    }
}

static const MemoryRegionOps esp32c3_i2s_ops = {
    .read = esp32c3_i2s_read,
    .write = esp32c3_i2s_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};


static void esp32c3_i2s_reset_hold(Object *obj, ResetType type)
{
    Esp32C3I2SState *s = ESP32C3_I2S(obj);

    timer_del(&s->tx_timer);
    s->tx_running = false;
    memset(s->regs, 0, sizeof(s->regs));
    qemu_irq_lower(s->irq);
}

static void esp32c3_i2s_realize(DeviceState *dev, Error **errp)
{
    Esp32C3I2SState *s = ESP32C3_I2S(dev);

    if (s->gdma == NULL) {
        error_setg(errp, "[I2S] GDMA controller must be set before realize");
        return;
    }

    timer_init_ns(&s->tx_timer, QEMU_CLOCK_VIRTUAL, esp32c3_i2s_tx_tick, s);
}

static void esp32c3_i2s_init(Object *obj)
{
    Esp32C3I2SState *s = ESP32C3_I2S(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_i2s_ops, s,
                          TYPE_ESP32C3_I2S, ESP32C3_I2S_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property esp32c3_i2s_properties[] = {
    DEFINE_PROP_CHR("chardev", Esp32C3I2SState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c3_i2s_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp32c3_i2s_realize;
    rc->phases.hold = esp32c3_i2s_reset_hold;
    device_class_set_props(dc, esp32c3_i2s_properties);
}

static const TypeInfo esp32c3_i2s_info = {
    .name = TYPE_ESP32C3_I2S,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32C3I2SState),
    .instance_init = esp32c3_i2s_init,
    .class_init = esp32c3_i2s_class_init,
};

static void esp32c3_i2s_register_types(void)
{
    type_register_static(&esp32c3_i2s_info);
}

type_init(esp32c3_i2s_register_types)

/*
 * ESP32-C3 SAR ADC emulation (oneshot mode only)
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Just enough of APB_SARADC for ESP-IDF's oneshot driver. The reason it
 * exists at all is that the driver waits for a conversion with
 *
 *     while (!adc_oneshot_ll_get_event(event));
 *
 * and no timeout, in adc_oneshot_hal_convert() and, more to the point, in
 * the self-calibration that esp_adc runs from a constructor on every boot
 * when the eFuses hold no calibration data -- which is always, here. With no
 * device behind these registers INT_RAW reads 0 forever and the guest never
 * reaches app_main().
 *
 * A conversion completes on the write that starts it, and yields the raw
 * code configured for the selected pad. Continuous (DMA) mode, the threshold
 * monitors, the filters and the temperature sensor are plain storage.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/adc/esp32c3_saradc.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"

static void esp32c3_saradc_update_irq(Esp32C3SarAdcState *s)
{
    qemu_set_irq(s->irq, !!(s->regs[R_SARADC_INT_RAW] &
                            s->regs[R_SARADC_INT_ENA]));
}

/*
 * The code the selected input converts to. ONETIME_CHANNEL is (unit << 3) |
 * channel, so 0..4 are ADC1's pads and 8 is ADC2's only one.
 *
 * Everything else reads 0. That covers the two values the HAL writes to
 * "disable" the channel while it self-calibrates against the internal ground
 * (0xf for ADC1, 0x9 for ADC2), and 0 is what a calibrated ADC reads there:
 * adc_hal_self_calibration() binary-searches for the smallest init code that
 * still reads 0, so a constant 0 converges at once, on a non-zero code that
 * the caller then caches.
 */
static uint32_t esp32c3_saradc_sample(Esp32C3SarAdcState *s, uint32_t channel)
{
    if (channel < 5) {
        return s->pad_raw[channel];
    }
    if (channel == 8) {
        return s->pad_raw[5];
    }
    return 0;
}

static void esp32c3_saradc_convert(Esp32C3SarAdcState *s)
{
    const uint32_t sample = s->regs[R_SARADC_ONETIME_SAMPLE];
    const uint32_t channel = FIELD_EX32(sample, SARADC_ONETIME_SAMPLE,
                                        ONETIME_CHANNEL);
    /*
     * The data word carries the channel above the 12-bit code, as on the
     * hardware. adc_oneshot_ll_raw_check_valid() reads it back out of bits
     * 16:13 and rejects an ADC2 result whose value there is above 9.
     */
    const uint32_t data = (channel << 13) |
        (esp32c3_saradc_sample(s, channel) & ESP32C3_SARADC_RAW_MAX);

    if (FIELD_EX32(sample, SARADC_ONETIME_SAMPLE, ADC1_ONETIME_SAMPLE)) {
        s->regs[R_SARADC_1_DATA_STATUS] = data;
        s->regs[R_SARADC_INT_RAW] |= R_SARADC_INT_RAW_ADC1_DONE_MASK;
    }
    if (FIELD_EX32(sample, SARADC_ONETIME_SAMPLE, ADC2_ONETIME_SAMPLE)) {
        s->regs[R_SARADC_2_DATA_STATUS] = data;
        s->regs[R_SARADC_INT_RAW] |= R_SARADC_INT_RAW_ADC2_DONE_MASK;
    }
}

static uint64_t esp32c3_saradc_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Esp32C3SarAdcState *s = ESP32C3_SARADC(opaque);
    const hwaddr idx = addr / sizeof(uint32_t);

    if (idx >= ESP32C3_SARADC_REG_COUNT) {
        return 0;
    }

    switch (idx) {
    case R_SARADC_INT_ST:
        return s->regs[R_SARADC_INT_RAW] & s->regs[R_SARADC_INT_ENA];
    case R_SARADC_INT_CLR:
        /* Write-only. The HAL does `int_clr.val |= mask`, so it is read */
        return 0;
    default:
        return s->regs[idx];
    }
}

static void esp32c3_saradc_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size)
{
    Esp32C3SarAdcState *s = ESP32C3_SARADC(opaque);
    const hwaddr idx = addr / sizeof(uint32_t);
    uint32_t old;

    if (idx >= ESP32C3_SARADC_REG_COUNT) {
        return;
    }

    switch (idx) {
    case R_SARADC_ONETIME_SAMPLE:
        old = s->regs[idx];
        s->regs[idx] = value;
        /*
         * Only a rising ONETIME_START converts. The HAL updates this register
         * a bitfield at a time and leaves START set between conversions, so
         * acting on the level would convert on every one of those writes --
         * and set DONE again right after the driver cleared it.
         */
        if (!FIELD_EX32(old, SARADC_ONETIME_SAMPLE, ONETIME_START) &&
            FIELD_EX32(value, SARADC_ONETIME_SAMPLE, ONETIME_START)) {
            esp32c3_saradc_convert(s);
        }
        break;

    case R_SARADC_INT_CLR:
        s->regs[R_SARADC_INT_RAW] &= ~(uint32_t)value;
        break;

    case R_SARADC_INT_RAW:
    case R_SARADC_INT_ST:
    case R_SARADC_1_DATA_STATUS:
    case R_SARADC_2_DATA_STATUS:
        /* Read-only */
        break;

    default:
        /*
         * Storage, so that the HAL's read-modify-write of one bitfield does
         * not wipe out the fields it set a moment earlier.
         */
        s->regs[idx] = value;
        break;
    }

    esp32c3_saradc_update_irq(s);
}

static const MemoryRegionOps esp32c3_saradc_ops = {
    .read = esp32c3_saradc_read,
    .write = esp32c3_saradc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_saradc_reset_hold(Object *obj, ResetType type)
{
    Esp32C3SarAdcState *s = ESP32C3_SARADC(obj);

    /* pad_raw is the board's wiring rather than device state: it stays */
    memset(s->regs, 0, sizeof(s->regs));
    esp32c3_saradc_update_irq(s);
}

static void esp32c3_saradc_realize(DeviceState *dev, Error **errp)
{
    Esp32C3SarAdcState *s = ESP32C3_SARADC(dev);

    for (int i = 0; i < ESP32C3_SARADC_PAD_COUNT; i++) {
        if (s->pad_raw[i] > ESP32C3_SARADC_RAW_MAX) {
            error_setg(errp, "pad%d: raw code %u is out of range 0..%d",
                       i, s->pad_raw[i], ESP32C3_SARADC_RAW_MAX);
            return;
        }
    }
}

static void esp32c3_saradc_init(Object *obj)
{
    Esp32C3SarAdcState *s = ESP32C3_SARADC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_saradc_ops, s,
                          TYPE_ESP32C3_SARADC, ESP32C3_SARADC_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

#define SARADC_PROP_PAD(n) \
    DEFINE_PROP_UINT16("pad" #n, Esp32C3SarAdcState, pad_raw[n], \
                       ESP32C3_SARADC_RAW_DEFAULT)

static Property esp32c3_saradc_properties[] = {
    SARADC_PROP_PAD(0),
    SARADC_PROP_PAD(1),
    SARADC_PROP_PAD(2),
    SARADC_PROP_PAD(3),
    SARADC_PROP_PAD(4),
    SARADC_PROP_PAD(5),
    DEFINE_PROP_END_OF_LIST()
};

static void esp32c3_saradc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp32c3_saradc_realize;
    device_class_set_props(dc, esp32c3_saradc_properties);
    rc->phases.hold = esp32c3_saradc_reset_hold;
}

static const TypeInfo esp32c3_saradc_type_info = {
    .name = TYPE_ESP32C3_SARADC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32C3SarAdcState),
    .instance_init = esp32c3_saradc_init,
    .class_init = esp32c3_saradc_class_init,
};

static void esp32c3_saradc_register_types(void)
{
    type_register_static(&esp32c3_saradc_type_info);
}

type_init(esp32c3_saradc_register_types)

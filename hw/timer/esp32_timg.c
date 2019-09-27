/*
 * ESP32 "Timer Group" peripheral
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/timer/esp32_timg.h"


#define TIMG_REGFILE_SIZE 0x100

static void esp32_timg_do_calibration(Esp32TimgState* s)
{
    uint32_t cal_clk_freq;
    if (s->rtc_cal_clk_sel == ESP32_TIMG_CAL_RTC_MUX) {
        cal_clk_freq = s->rtc_slow_freq_hz;
    } else if (s->rtc_cal_clk_sel == ESP32_TIMG_CAL_32K_XTAL) {
        cal_clk_freq = 32768;
    } else {
        cal_clk_freq = 8000000 / 256;
    }

    s->rtc_cal_value = muldiv64(s->xtal_freq_hz, s->rtc_cal_max, cal_clk_freq);
    s->rtc_cal_ready = true;
}

static uint64_t esp32_timg_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32TimgState *s = ESP32_TIMG(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_TIMG_RTCCALICFG:
        r = FIELD_DP32(r, TIMG_RTCCALICFG, START, s->rtc_cal_start);
        r = FIELD_DP32(r, TIMG_RTCCALICFG, MAX, s->rtc_cal_max);
        r = FIELD_DP32(r, TIMG_RTCCALICFG, RDY, s->rtc_cal_ready);
        r = FIELD_DP32(r, TIMG_RTCCALICFG, CLK_SEL, s->rtc_cal_clk_sel);
        break;
    case A_TIMG_RTCCALICFG1:
        r = FIELD_DP32(0, TIMG_RTCCALICFG1, VALUE, s->rtc_cal_value);
        break;
    }
    return r;
}

static void esp32_timg_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32TimgState *s = ESP32_TIMG(opaque);
    switch (addr) {
    case A_TIMG_RTCCALICFG:
        s->rtc_cal_start = FIELD_EX32(value, TIMG_RTCCALICFG, START);
        s->rtc_cal_ready = FIELD_EX32(value, TIMG_RTCCALICFG, RDY);
        s->rtc_cal_clk_sel = FIELD_EX32(value, TIMG_RTCCALICFG, CLK_SEL);
        s->rtc_cal_max = FIELD_EX32(value, TIMG_RTCCALICFG, MAX);
        esp32_timg_do_calibration(s);
        break;
    }

}

static const MemoryRegionOps esp32_timg_ops = {
    .read =  esp32_timg_read,
    .write = esp32_timg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_timg_reset(DeviceState *dev)
{
    Esp32TimgState *s = ESP32_TIMG(dev);
    s->rtc_cal_max = 1;
    s->rtc_cal_clk_sel = ESP32_TIMG_CAL_8MD256;
    s->rtc_cal_ready = 0;
    s->rtc_cal_start = 1;
    esp32_timg_do_calibration(s);
}

static void esp32_timg_realize(DeviceState *dev, Error **errp)
{
}

static void esp32_timg_init(Object *obj)
{
    Esp32TimgState *s = ESP32_TIMG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_timg_ops, s,
                          TYPE_ESP32_TIMG, TIMG_REGFILE_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->rtc_slow_freq_hz = 150000;
    s->xtal_freq_hz = 40000000;
}

static Property esp32_timg_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_timg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_timg_reset;
    dc->realize = esp32_timg_realize;
    dc->props = esp32_timg_properties;
}

static const TypeInfo esp32_timg_info = {
    .name = TYPE_ESP32_TIMG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32TimgState),
    .instance_init = esp32_timg_init,
    .class_init = esp32_timg_class_init
};

static void esp32_timg_register_types(void)
{
    type_register_static(&esp32_timg_info);
}

type_init(esp32_timg_register_types)

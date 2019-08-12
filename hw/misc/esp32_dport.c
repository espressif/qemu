/*
 * ESP32 "DPORT" device
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
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/misc/esp32_reg.h"
#include "hw/misc/esp32_dport.h"
#include "target/xtensa/cpu.h"


#define ESP32_DPORT_SIZE   (DR_REG_DPORT_END - DR_REG_DPORT_BASE)


static uint64_t esp32_dport_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32DportState *s = ESP32_DPORT(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_DPORT_APPCPU_RESET:
        r = s->appcpu_reset_state;
        break;
    case A_DPORT_APPCPU_CLK:
        r = s->appcpu_clkgate_state;
        break;
    case A_DPORT_APPCPU_RUNSTALL:
        r = s->appcpu_stall_state;
        break;
    case A_DPORT_APPCPU_BOOT_ADDR:
        r = s->appcpu_boot_addr;
        break;
    case A_DPORT_PRO_CACHE_CTRL:
        r = s->cache_state[0].cache_ctrl_reg;
        break;
    case A_DPORT_PRO_CACHE_CTRL1:
        r = s->cache_state[0].cache_ctrl1_reg;
        break;
    case A_DPORT_APP_CACHE_CTRL:
        r = s->cache_state[1].cache_ctrl_reg;
        break;
    case A_DPORT_APP_CACHE_CTRL1:
        r = s->cache_state[1].cache_ctrl1_reg;
        break;
    }

    return r;
}

static void esp32_dport_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    Esp32DportState *s = ESP32_DPORT(opaque);
    bool old_state;
    switch (addr) {
    case A_DPORT_APPCPU_RESET:
        old_state = s->appcpu_reset_state;
        s->appcpu_reset_state = value & 1;
        if (old_state && !s->appcpu_reset_state) {
            qemu_irq_pulse(s->appcpu_reset_req);
        }
        break;
    case A_DPORT_APPCPU_CLK:
        s->appcpu_clkgate_state = value & 1;
        qemu_set_irq(s->appcpu_stall_req, s->appcpu_stall_state || !s->appcpu_clkgate_state);
        break;
    case A_DPORT_APPCPU_RUNSTALL:
        s->appcpu_stall_state = value & 1;
        qemu_set_irq(s->appcpu_stall_req, s->appcpu_stall_state || !s->appcpu_clkgate_state);
        break;
    case A_DPORT_APPCPU_BOOT_ADDR:
        s->appcpu_boot_addr = value;
        break;
    case A_DPORT_PRO_CACHE_CTRL:
        if (FIELD_EX32(value, DPORT_PRO_CACHE_CTRL, CACHE_FLUSH_ENA)) {
            s->cache_state[0].cache_ctrl_reg |= R_DPORT_PRO_CACHE_CTRL_CACHE_FLUSH_DONE_MASK;
        }
        break;
    case A_DPORT_PRO_CACHE_CTRL1:
        s->cache_state[0].cache_ctrl1_reg = value;
        break;
    case A_DPORT_APP_CACHE_CTRL:
        if (FIELD_EX32(value, DPORT_APP_CACHE_CTRL, CACHE_FLUSH_ENA)) {
            s->cache_state[1].cache_ctrl_reg |= R_DPORT_APP_CACHE_CTRL_CACHE_FLUSH_DONE_MASK;
        }
        break;
    case A_DPORT_APP_CACHE_CTRL1:
        s->cache_state[1].cache_ctrl1_reg = value;
        break;
    }
}

static const MemoryRegionOps esp32_dport_ops = {
    .read =  esp32_dport_read,
    .write = esp32_dport_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_dport_reset(DeviceState *dev)
{
    Esp32DportState *s = ESP32_DPORT(dev);

    s->appcpu_boot_addr = 0;
    s->appcpu_clkgate_state = false;
    s->appcpu_reset_state = true;
    s->appcpu_stall_state = false;
}

static void esp32_dport_realize(DeviceState *dev, Error **errp)
{
    Esp32DportState *s = ESP32_DPORT(dev);
    MachineState *ms = MACHINE(qdev_get_machine());

    s->cpu_count = ms->smp.cpus;

    for (int i = 0; i < s->cpu_count; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "cpu%d", i);
        object_property_set_link(OBJECT(&s->intmatrix), OBJECT(qemu_get_cpu(i)), name, &error_abort);
    }
    object_property_set_bool(OBJECT(&s->intmatrix), true, "realized", &error_abort);

    object_property_set_bool(OBJECT(&s->crosscore_int), true, "realized", &error_abort);

    for (int index = 0; index < ESP32_DPORT_CROSSCORE_INT_COUNT; ++index) {
        qemu_irq target = qdev_get_gpio_in(DEVICE(&s->intmatrix), ETS_FROM_CPU_INTR0_SOURCE + index);
        assert(target);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->crosscore_int), index, target);
    }
}

static void esp32_dport_init(Object *obj)
{
    Esp32DportState *s = ESP32_DPORT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_dport_ops, s,
                          TYPE_ESP32_DPORT, ESP32_DPORT_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    object_initialize_child(obj, "intmatrix", &s->intmatrix, sizeof(s->intmatrix), TYPE_ESP32_INTMATRIX, &error_abort, NULL);
    memory_region_add_subregion_overlap(&s->iomem, ESP32_DPORT_PRO_INTMATRIX_BASE, &s->intmatrix.iomem, -1);

    object_initialize_child(obj, "crosscore_int", &s->crosscore_int, sizeof(s->crosscore_int), TYPE_ESP32_CROSSCORE_INT,
                            &error_abort, NULL);
    memory_region_add_subregion_overlap(&s->iomem, ESP32_DPORT_CROSSCORE_INT_BASE, &s->crosscore_int.iomem, -1);

    qdev_init_gpio_out_named(DEVICE(sbd), &s->appcpu_stall_req, ESP32_DPORT_APPCPU_STALL_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->appcpu_reset_req, ESP32_DPORT_APPCPU_RESET_GPIO, 1);
}

static Property esp32_dport_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_dport_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_dport_reset;
    dc->realize = esp32_dport_realize;
    dc->props = esp32_dport_properties;
}

static const TypeInfo esp32_dport_info = {
    .name = TYPE_ESP32_DPORT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32DportState),
    .instance_init = esp32_dport_init,
    .class_init = esp32_dport_class_init
};

static void esp32_dport_register_types(void)
{
    type_register_static(&esp32_dport_info);
}

type_init(esp32_dport_register_types)

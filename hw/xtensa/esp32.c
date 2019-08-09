/*
 * ESP32 SoC and machine
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
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/xtensa/cpu.h"
#include "hw/misc/esp32_reg.h"
#include "hw/char/esp32_uart.h"
#include "hw/misc/esp32_gpio.h"
#include "hw/misc/esp32_dport.h"
#include "hw/misc/esp32_rtc_cntl.h"
#include "hw/xtensa/xtensa_memory.h"
#include "hw/misc/unimp.h"
#include "elf.h"

#define TYPE_ESP32_SOC "xtensa.esp32"
#define ESP32_SOC(obj) OBJECT_CHECK(Esp32SocState, (obj), TYPE_ESP32_SOC)

#define TYPE_ESP32_CPU XTENSA_CPU_TYPE_NAME("esp32")

typedef struct XtensaCPU XtensaCPU;



enum {
    ESP32_MEMREGION_IROM,
    ESP32_MEMREGION_DROM,
    ESP32_MEMREGION_DRAM,
    ESP32_MEMREGION_IRAM,
    ESP32_MEMREGION_ICACHE0,
    ESP32_MEMREGION_ICACHE1,
    ESP32_MEMREGION_UART
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32_memmap[] = {
    [ESP32_MEMREGION_DROM] = { 0x3ff90000, 0x10000 },
    [ESP32_MEMREGION_IROM] = { 0x40000000, 0x70000 },
    [ESP32_MEMREGION_DRAM] = { 0x3ffae000, 0x52000 },
    [ESP32_MEMREGION_IRAM] = { 0x40080000, 0x20000 },
    [ESP32_MEMREGION_ICACHE0] = { 0x40070000, 0x8000 },
    [ESP32_MEMREGION_ICACHE1] = { 0x40078000, 0x8000 },
    [ESP32_MEMREGION_UART] = { 0x3ff40000, 0x1000 },
};

typedef struct Esp32SocState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    XtensaCPU cpu[ESP32_CPU_COUNT];
    Esp32DportState dport;
    ESP32UARTState uart;
    Esp32GpioState gpio;
    Esp32RtcCntlState rtc_cntl;
} Esp32SocState;


static void esp32_soc_reset(DeviceState *dev)
{
    Esp32SocState *s = ESP32_SOC(dev);
    MachineState *ms = MACHINE(qdev_get_machine());

    for (int i = 0; i < ms->smp.cpus; ++i) {
        cpu_reset(CPU(&s->cpu));
        xtensa_runstall(&s->cpu[i].env, i != 0);
    }
}

static void esp32_soc_realize(DeviceState *dev, Error **errp)
{
    Esp32SocState *s = ESP32_SOC(dev);
    MachineState *ms = MACHINE(qdev_get_machine());

    const struct MemmapEntry *memmap = esp32_memmap;
    MemoryRegion *sys_mem = get_system_memory();

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    MemoryRegion *iram = g_new(MemoryRegion, 1);
    MemoryRegion *drom = g_new(MemoryRegion, 1);
    MemoryRegion *irom = g_new(MemoryRegion, 1);
    MemoryRegion *icache0 = g_new(MemoryRegion, 1);
    MemoryRegion *icache1 = g_new(MemoryRegion, 1);

    memory_region_init_ram(irom, NULL, "esp32.irom",
                           memmap[ESP32_MEMREGION_IROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32_MEMREGION_IROM].base, irom);

    memory_region_init_alias(drom, NULL, "esp32.drom", irom, 0x60000, memmap[ESP32_MEMREGION_DROM].size);
    memory_region_add_subregion(sys_mem, memmap[ESP32_MEMREGION_DROM].base, drom);

    memory_region_init_ram(dram, NULL, "esp32.dram",
                           memmap[ESP32_MEMREGION_DRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32_MEMREGION_DRAM].base, dram);

    memory_region_init_ram(iram, NULL, "esp32.iram",
                           memmap[ESP32_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32_MEMREGION_IRAM].base, iram);

    memory_region_init_ram(icache0, NULL, "esp32.icache0",
                               memmap[ESP32_MEMREGION_ICACHE0].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32_MEMREGION_ICACHE0].base, icache0);

    memory_region_init_ram(icache1, NULL, "esp32.icache1",
                                   memmap[ESP32_MEMREGION_ICACHE1].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32_MEMREGION_ICACHE1].base, icache1);

    for (int i = 0; i < ms->smp.cpus; ++i) {
        object_property_set_bool(OBJECT(&s->cpu[i]), true, "realized", &error_abort);
    }

    object_property_set_bool(OBJECT(&s->uart), true, "realized", &error_abort);

    MemoryRegion *uart = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0);
    memory_region_add_subregion_overlap(sys_mem, memmap[ESP32_MEMREGION_UART].base, uart, 0);

    object_property_set_bool(OBJECT(&s->gpio), true, "realized", &error_abort);

    MemoryRegion *gpio = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, gpio, 0);

    object_property_set_bool(OBJECT(&s->dport), true, "realized", &error_abort);

    MemoryRegion *dport = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dport), 0);
    memory_region_add_subregion(sys_mem, DR_REG_DPORT_BASE, dport);

    object_property_set_bool(OBJECT(&s->rtc_cntl), true, "realized", &error_abort);

    MemoryRegion *rtc_cntl = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rtc_cntl), 0);
    memory_region_add_subregion(sys_mem, DR_REG_RTCCNTL_BASE, rtc_cntl);

    create_unimplemented_device("esp32.frc0", DR_REG_FRC_TIMER_BASE, 0x1000);
    create_unimplemented_device("esp32.rtcio", DR_REG_RTCIO_BASE, 0x400);
    create_unimplemented_device("esp32.uart1", DR_REG_UART1_BASE, 0x1000);
    create_unimplemented_device("esp32.tg0", DR_REG_TIMERGROUP0_BASE, 0x1000);
    create_unimplemented_device("esp32.tg1", DR_REG_TIMERGROUP1_BASE, 0x1000);
    create_unimplemented_device("esp32.efuse", DR_REG_EFUSE_BASE, 0x1000);
    create_unimplemented_device("esp32.iomux", DR_REG_IO_MUX_BASE, 0x2000);
    create_unimplemented_device("esp32.hinf", DR_REG_HINF_BASE, 0x1000);
    create_unimplemented_device("esp32.slc", DR_REG_SLC_BASE, 0x1000);
    create_unimplemented_device("esp32.slchost", DR_REG_SLCHOST_BASE, 0x1000);
    create_unimplemented_device("esp32.spi0", DR_REG_SPI0_BASE, 0x1000);
    create_unimplemented_device("esp32.spi1", DR_REG_SPI1_BASE, 0x1000);

}

static void esp32_soc_init(Object *obj)
{
    Esp32SocState *s = ESP32_SOC(obj);
    MachineState *ms = MACHINE(qdev_get_machine());

    for (int i = 0; i < ms->smp.cpus; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "cpu%d", i);
        object_initialize_child(obj, name, &s->cpu[i], sizeof(s->cpu[i]), TYPE_ESP32_CPU, &error_abort, NULL);
    }

    object_initialize_child(obj, "uart", &s->uart, sizeof(s->uart),
                            TYPE_ESP32_UART, &error_abort, NULL);

    object_property_add_alias(obj, "serial0", OBJECT(&s->uart), "chardev",
                              &error_abort);

    object_initialize_child(obj, "gpio", &s->gpio, sizeof(s->gpio),
                                TYPE_ESP32_GPIO, &error_abort, NULL);

    object_initialize_child(obj, "dport", &s->dport, sizeof(s->dport),
                            TYPE_ESP32_DPORT, &error_abort, NULL);

    object_initialize_child(obj, "rtc_cntl", &s->rtc_cntl, sizeof(s->rtc_cntl),
                            TYPE_ESP32_RTC_CNTL, &error_abort, NULL);
}

static Property esp32_soc_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_soc_reset;
    dc->realize = esp32_soc_realize;
    dc->props = esp32_soc_properties;
}

static const TypeInfo esp32_soc_info = {
    .name = TYPE_ESP32_SOC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32SocState),
    .instance_init = esp32_soc_init,
    .class_init = esp32_soc_class_init
};

static void esp32_soc_register_types(void)
{
    type_register_static(&esp32_soc_info);
}

type_init(esp32_soc_register_types)


static uint64_t translate_phys_addr(void *opaque, uint64_t addr)
{
    XtensaCPU *cpu = opaque;

    return cpu_get_phys_page_debug(CPU(cpu), addr);
}

static void esp32_reset(void *opaque)
{
    Esp32SocState *s = ESP32_SOC(opaque);
    esp32_soc_reset(DEVICE(s));
}

static void esp32_machine_inst_init(MachineState *machine)
{
    Esp32SocState *s = g_new0(Esp32SocState, 1);

    object_initialize_child(OBJECT(machine), "soc", s, sizeof(*s),
                            TYPE_ESP32_SOC, &error_abort, NULL);
    object_property_set_bool(OBJECT(s), true, "realized", &error_abort);


    qemu_register_reset(esp32_reset, s);

    /* Need MMU initialized prior to ELF loading,
     * so that ELF gets loaded into virtual addresses
     */
    esp32_reset(s);

    if (machine->kernel_filename) {
        uint64_t elf_entry;
        uint64_t elf_lowaddr;
        int success = load_elf(machine->kernel_filename, NULL,
                               translate_phys_addr, &s->cpu[0],
                               &elf_entry, &elf_lowaddr,
                               NULL, 0, EM_XTENSA, 0, 0);
        if (success > 0) {
            s->cpu[0].env.pc = elf_entry;
        }
    }
}

/* Initialize machine type */
static void esp32_machine_init(MachineClass *mc)
{
    mc->desc = "Espressif ESP32 machine";
    mc->init = esp32_machine_inst_init;
    mc->max_cpus = 2;
    mc->default_cpus = 2;
}

DEFINE_MACHINE("esp32", esp32_machine_init)


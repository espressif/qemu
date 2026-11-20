/*
 * Copyright (c) 2014, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ESP8266/ESP8285 SoC and machines, ported from Max Filippov's
 * xtensa-esp8266 branch (OSLL/qemu-xtensa, 2014-2019). Boots the Espressif
 * mask ROM (-bios esp8266.rom, not distributed) and a flash image
 * (-drive if=mtd, or a raw -kernel file).
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/loader.h"
#include "hw/xtensa/esp8266.h"
#include "exec/address-spaces.h"
#include "chardev/char.h"
#include "net/net.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "cpu.h"

/*
 * External interrupt pins of the lx106 core: pins 0-5 are INUM 0-5
 * (level), pins 6-11 INUM 8-13 (edge), pin 12 the NMI (INUM 14).
 * ESP8266: WiFi MAC 0, SPI 2, GPIO 4, UART 5, FRC1 9, FRC2 10.
 */
enum {
    ESP8266_EXTINT_WIFI = 0,
    ESP8266_EXTINT_SPI = 2,
    ESP8266_EXTINT_GPIO = 4,
    ESP8266_EXTINT_UART = 5,
    ESP8266_EXTINT_FRC1 = 7,
    ESP8266_EXTINT_FRC2 = 8,
    ESP8266_EXTINT_NMI = 12,
};

/* Offsets in the 0x60000000 peripheral window */
enum {
    ESP8266_IO_UART0 = 0x0000,
    ESP8266_IO_HSPI = 0x0100,
    ESP8266_IO_SPI0 = 0x0200,
    ESP8266_IO_GPIO = 0x0300,
    ESP8266_IO_FRC = 0x0600,
    ESP8266_IO_RTC = 0x0700,
    ESP8266_IO_IOMUX = 0x0800,
    ESP8266_IO_ANA = 0x0d00,
    ESP8266_IO_SAR = 0x0d50,
    ESP8266_IO_RTC_MEM = 0x1000,
};

#define ESP8266_DPORT_BASE      0x3ff00000
#define ESP8266_DRAM_BASE       0x3ff10000  /* DRAM, IRAM and the ROM window */
#define ESP8266_DRAM_SIZE       0x002f0000
#define ESP8266_RNG_BASE        0x3ff20e44
#define ESP8266_ROM_BASE        0x40000000
#define ESP8266_FLASH_BASE      0x40200000
#define ESP8266_IO_BASE         0x60000000

/*
 * Unmodelled peripherals in the 0x60000000 window read as 0 (visible with
 * -d unimp), except two registers the SDK waits on.
 */
static uint64_t esp8266_io_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "unassigned: read +0x%08x\n", (uint32_t)addr);
    switch (addr) {
    case 0x057c:
        return 0x80000000;
    case 0x0d4c:
        return 0x01000000;
    }
    return 0;
}

static void esp8266_io_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "unassigned: write +0x%08x = %08x\n",
                  (uint32_t)addr, (uint32_t)val);
}

static const MemoryRegionOps esp8266_io_ops = {
    .read = esp8266_io_read,
    .write = esp8266_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void esp8266_soc_cpu_reset(void *opaque)
{
    cpu_reset(CPU(opaque));
}

static void esp8266_soc_map(Esp8266SocState *s, void *dev, int n,
                            hwaddr offset)
{
    memory_region_add_subregion(&s->io, offset,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), n));
}

static void esp8266_soc_realize(DeviceState *dev, Error **errp)
{
    Esp8266SocState *s = ESP8266_SOC(dev);
    MemoryRegion *sysmem = get_system_memory();
    CPUXtensaState *env;
    qemu_irq *extint;

    object_initialize_child(OBJECT(s), "cpu", &s->cpu, s->cpu_type);
    qdev_connect_clock_in(DEVICE(&s->cpu), "clk-in",
                          qdev_get_clock_out(DEVICE(&s->dport), "cpu-clk"));
    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }
    env = &s->cpu.env;
    env->sregs[PRID] = 0;
    qemu_register_reset(esp8266_soc_cpu_reset, &s->cpu);
    xtensa_select_static_vectors(env, 1);
    cpu_reset(CPU(&s->cpu));
    extint = xtensa_get_extints(env);

    if (!memory_region_init_ram(&s->dram, OBJECT(s), "esp8266.dram",
                                ESP8266_DRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(sysmem, ESP8266_DRAM_BASE, &s->dram);
    memory_region_init_io(&s->io, OBJECT(s), &esp8266_io_ops, s, "esp8266.io",
                          256 * MiB);
    memory_region_add_subregion(sysmem, ESP8266_IO_BASE, &s->io);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rng), errp)) {
        return;
    }
    memory_region_add_subregion_overlap(sysmem, ESP8266_RNG_BASE,
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rng), 0), 2);

    /* Always present: WiFi firmware faults on a missing MAC. */
    qemu_configure_nic_device(DEVICE(&s->wifi), false, NULL);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wifi), errp)) {
        return;
    }
    for (unsigned i = 0; i < ESP8266_WIFI_PAGE_COUNT; i++) {
        memory_region_add_subregion_overlap(sysmem,
                        ESP8266_WIFI_BASE + i * ESP8266_WIFI_PAGE_SIZE,
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->wifi), i), 1);
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->wifi), 0,
                       extint[ESP8266_EXTINT_WIFI]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart), errp)) {
        return;
    }
    esp8266_soc_map(s, &s->uart, 0, ESP8266_IO_UART0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart), 0,
                       extint[ESP8266_EXTINT_UART]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi), errp)) {
        return;
    }
    esp8266_soc_map(s, &s->spi, 0, ESP8266_IO_SPI0);
    memory_region_add_subregion(sysmem, ESP8266_FLASH_BASE,
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->spi), 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi), 0, extint[ESP8266_EXTINT_SPI]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    esp8266_soc_map(s, &s->gpio, 0, ESP8266_IO_GPIO);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), 0,
                       extint[ESP8266_EXTINT_GPIO]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hspi), errp)) {
        return;
    }
    esp8266_soc_map(s, &s->hspi, 0, ESP8266_IO_HSPI);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hspi), 0,
                       extint[ESP8266_EXTINT_SPI]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc), errp)) {
        return;
    }
    esp8266_soc_map(s, &s->rtc, 0, ESP8266_IO_RTC);
    esp8266_soc_map(s, &s->rtc, 1, ESP8266_IO_RTC_MEM);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->iomux), errp) ||
        !sysbus_realize(SYS_BUS_DEVICE(&s->ana), errp) ||
        !sysbus_realize(SYS_BUS_DEVICE(&s->sar), errp)) {
        return;
    }
    esp8266_soc_map(s, &s->iomux, 0, ESP8266_IO_IOMUX);
    esp8266_soc_map(s, &s->ana, 0, ESP8266_IO_ANA);
    esp8266_soc_map(s, &s->sar, 0, ESP8266_IO_SAR);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dport), errp)) {
        return;
    }
    memory_region_add_subregion(sysmem, ESP8266_DPORT_BASE,
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dport), 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->frc), errp)) {
        return;
    }
    esp8266_soc_map(s, &s->frc, 0, ESP8266_IO_FRC);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->frc), 0,
                       extint[ESP8266_EXTINT_FRC1]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->frc), 1,
                       extint[ESP8266_EXTINT_FRC2]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->frc), 2, extint[ESP8266_EXTINT_NMI]);

    qdev_connect_gpio_out_named(DEVICE(&s->dport),
                                ESP8266_DPORT_FRC_NMI_ROUTE, 0,
                                qdev_get_gpio_in_named(DEVICE(&s->frc),
                                                ESP8266_FRC_NMI_ROUTE_GPIO, 0));
    qdev_connect_gpio_out_named(DEVICE(&s->dport),
                                ESP8266_DPORT_WIFI_MAC_RST, 0,
                                qdev_get_gpio_in_named(DEVICE(&s->wifi),
                                                ESP8266_WIFI_MAC_RST_GPIO, 0));
}

static void esp8266_soc_init(Object *obj)
{
    Esp8266SocState *s = ESP8266_SOC(obj);

    object_initialize_child(obj, "dport", &s->dport, TYPE_ESP8266_DPORT);
    object_initialize_child(obj, "uart0", &s->uart, TYPE_ESP8266_UART);
    object_initialize_child(obj, "spi0", &s->spi, TYPE_ESP8266_SPI);
    object_initialize_child(obj, "hspi", &s->hspi, TYPE_ESP8266_HSPI);
    object_initialize_child(obj, "gpio", &s->gpio, TYPE_ESP8266_GPIO);
    object_initialize_child(obj, "frc", &s->frc, TYPE_ESP8266_FRC);
    object_initialize_child(obj, "rtc", &s->rtc, TYPE_ESP8266_RTC);
    object_initialize_child(obj, "iomux", &s->iomux, TYPE_ESP8266_IOMUX);
    object_initialize_child(obj, "ana", &s->ana, TYPE_ESP8266_ANA);
    object_initialize_child(obj, "sar", &s->sar, TYPE_ESP8266_SAR);
    object_initialize_child(obj, "rng", &s->rng, TYPE_ESP8266_RNG);
    object_initialize_child(obj, "wifi", &s->wifi, TYPE_ESP8266_WIFI);

    object_property_add_alias(obj, "serial0", OBJECT(&s->uart), "chardev");
    object_property_add_alias(obj, "drive", OBJECT(&s->spi), "drive");
    object_property_add_alias(obj, "adc", OBJECT(&s->sar), "adc");
}

static Property esp8266_soc_properties[] = {
    DEFINE_PROP_STRING("cpu-type", Esp8266SocState, cpu_type),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp8266_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp8266_soc_realize;
    /* It creates the CPU and maps memory: only boards instantiate it. */
    dc->user_creatable = false;
    device_class_set_props(dc, esp8266_soc_properties);
}

/* Machines */

void esp8266_machine_init(MachineState *machine)
{
    Esp8266MachineState *ms = ESP8266_MACHINE(machine);
    const char *rom_name = machine->firmware ? machine->firmware
                                             : "esp8266.rom";
    DeviceState *soc;
    Chardev *serial0 = serial_hd(0);
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    char *rom_filename;

    object_initialize_child(OBJECT(machine), "soc", &ms->soc,
                            TYPE_ESP8266_SOC);
    soc = DEVICE(&ms->soc);
    qdev_prop_set_string(soc, "cpu-type", machine->cpu_type);
    if (!serial0) {
        serial0 = qemu_chr_new("serial0", "null", NULL);
    }
    qdev_prop_set_chr(soc, "serial0", serial0);
    if (dinfo) {
        /* Flash image: -drive if=mtd, as on the ESP32 boards. */
        qdev_prop_set_drive_err(soc, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }
    qdev_realize(soc, NULL, &error_fatal);
    object_property_add_alias(OBJECT(machine), "adc", OBJECT(soc), "adc");

    /* Or a raw flash image as -kernel. */
    if (!dinfo && machine->kernel_filename &&
        load_image_size(machine->kernel_filename,
                        memory_region_get_ram_ptr(
                            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->soc.spi),
                                                   1)),
                        ESP8266_SPI_FLASH_MAX) < 0) {
        error_report("esp8266: cannot load '%s'", machine->kernel_filename);
        exit(EXIT_FAILURE);
    }

    rom_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, rom_name);
    if ((!rom_filename ||
         load_image_targphys(rom_filename, ESP8266_ROM_BASE, 64 * KiB) < 0) &&
        !qtest_enabled()) {
        /* qtests (qom-test, test-hmp) start every machine without firmware. */
        error_report("esp8266: cannot load the mask ROM '%s' (dump it with "
                     "esptool dump_mem 0x40000000 65536; pass -bios or -L)",
                     rom_name);
        exit(EXIT_FAILURE);
    }
    g_free(rom_filename);
}

static void esp8266_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Espressif ESP8266/ESP8285 (lx106)";
    mc->init = esp8266_machine_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = XTENSA_CPU_TYPE_NAME("lx106");
}

static const TypeInfo esp8266_types[] = {
    {
        .name = TYPE_ESP8266_SOC,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(Esp8266SocState),
        .instance_init = esp8266_soc_init,
        .class_init = esp8266_soc_class_init,
    }, {
        .name = TYPE_ESP8266_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(Esp8266MachineState),
        .class_init = esp8266_machine_class_init,
    },
};

DEFINE_TYPES(esp8266_types)

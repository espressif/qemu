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
#include "hw/char/esp32_uart.h"
#include "hw/misc/esp32_gpio.h"
#include "hw/xtensa/xtensa_memory.h"
#include "hw/misc/unimp.h"
#include "elf.h"

#define TYPE_ESP32_SOC "xtensa.esp32"
#define ESP32_SOC(obj) OBJECT_CHECK(Esp32SocState, (obj), TYPE_ESP32_SOC)

#define ESP32_SOC_REG_CNT    0x100

#define TYPE_ESP32_CPU XTENSA_CPU_TYPE_NAME("esp32")

typedef struct XtensaCPU XtensaCPU;


#define DR_REG_DPORT_BASE                       0x3ff00000
#define DR_REG_AES_BASE                         0x3ff01000
#define DR_REG_RSA_BASE                         0x3ff02000
#define DR_REG_SHA_BASE                         0x3ff03000
#define DR_REG_FLASH_MMU_TABLE_PRO              0x3ff10000
#define DR_REG_FLASH_MMU_TABLE_APP              0x3ff12000
#define DR_REG_DPORT_END                        0x3ff13FFC
#define DR_REG_UART_BASE                        0x3ff40000
#define DR_REG_SPI1_BASE                        0x3ff42000
#define DR_REG_SPI0_BASE                        0x3ff43000
#define DR_REG_GPIO_BASE                        0x3ff44000
#define DR_REG_GPIO_SD_BASE                     0x3ff44f00
#define DR_REG_FE2_BASE                         0x3ff45000
#define DR_REG_FE_BASE                          0x3ff46000
#define DR_REG_FRC_TIMER_BASE                   0x3ff47000
#define DR_REG_RTCCNTL_BASE                     0x3ff48000
#define DR_REG_RTCIO_BASE                       0x3ff48400
#define DR_REG_SENS_BASE                        0x3ff48800
#define DR_REG_RTC_I2C_BASE                     0x3ff48C00
#define DR_REG_IO_MUX_BASE                      0x3ff49000
#define DR_REG_HINF_BASE                        0x3ff4B000
#define DR_REG_UHCI1_BASE                       0x3ff4C000
#define DR_REG_I2S_BASE                         0x3ff4F000
#define DR_REG_UART1_BASE                       0x3ff50000
#define DR_REG_BT_BASE                          0x3ff51000
#define DR_REG_I2C_EXT_BASE                     0x3ff53000
#define DR_REG_UHCI0_BASE                       0x3ff54000
#define DR_REG_SLCHOST_BASE                     0x3ff55000
#define DR_REG_RMT_BASE                         0x3ff56000
#define DR_REG_PCNT_BASE                        0x3ff57000
#define DR_REG_SLC_BASE                         0x3ff58000
#define DR_REG_LEDC_BASE                        0x3ff59000
#define DR_REG_EFUSE_BASE                       0x3ff5A000
#define DR_REG_SPI_ENCRYPT_BASE                 0x3ff5B000
#define DR_REG_NRX_BASE                         0x3ff5CC00
#define DR_REG_BB_BASE                          0x3ff5D000
#define DR_REG_PWM_BASE                         0x3ff5E000
#define DR_REG_TIMERGROUP0_BASE                 0x3ff5F000
#define DR_REG_TIMERGROUP1_BASE                 0x3ff60000
#define DR_REG_RTCMEM0_BASE                     0x3ff61000
#define DR_REG_RTCMEM1_BASE                     0x3ff62000
#define DR_REG_RTCMEM2_BASE                     0x3ff63000
#define DR_REG_SPI2_BASE                        0x3ff64000
#define DR_REG_SPI3_BASE                        0x3ff65000
#define DR_REG_SYSCON_BASE                      0x3ff66000
#define DR_REG_APB_CTRL_BASE                    0x3ff66000    /* Old name for SYSCON, to be removed */
#define DR_REG_I2C1_EXT_BASE                    0x3ff67000
#define DR_REG_SDMMC_BASE                       0x3ff68000
#define DR_REG_EMAC_BASE                        0x3ff69000
#define DR_REG_CAN_BASE                         0x3ff6B000
#define DR_REG_PWM1_BASE                        0x3ff6C000
#define DR_REG_I2S1_BASE                        0x3ff6D000
#define DR_REG_UART2_BASE                       0x3ff6E000
#define DR_REG_PWM2_BASE                        0x3ff6F000
#define DR_REG_PWM3_BASE                        0x3ff70000


enum {
    ESP32_MEMREGION_IROM,
    ESP32_MEMREGION_DROM,
    ESP32_MEMREGION_DRAM,
    ESP32_MEMREGION_IRAM,
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
    [ESP32_MEMREGION_UART] = { 0x3ff40000, 0x1000 },
};

typedef struct Esp32SocState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    XtensaCPU cpu;
    ESP32UARTState uart;
    Esp32GpioState gpio;
} Esp32SocState;


static void esp32_soc_reset(DeviceState *dev)
{
    Esp32SocState *s = ESP32_SOC(dev);
}

static void esp32_soc_realize(DeviceState *dev, Error **errp)
{
    Esp32SocState *s = ESP32_SOC(dev);

    const struct MemmapEntry *memmap = esp32_memmap;
    MemoryRegion *sys_mem = get_system_memory();

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    MemoryRegion *iram = g_new(MemoryRegion, 1);
    MemoryRegion *drom = g_new(MemoryRegion, 1);
    MemoryRegion *irom = g_new(MemoryRegion, 1);

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

    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &error_abort);

    object_property_set_bool(OBJECT(&s->uart), true, "realized", &error_abort);

    MemoryRegion *uart = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0);
    memory_region_add_subregion_overlap(sys_mem, memmap[ESP32_MEMREGION_UART].base, uart, 0);

    object_property_set_bool(OBJECT(&s->gpio), true, "realized", &error_abort);

    MemoryRegion *gpio = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, gpio, 0);

    create_unimplemented_device("esp32.dport", DR_REG_DPORT_BASE, 0x1000);
    create_unimplemented_device("esp32.rtc_cntl", DR_REG_RTCCNTL_BASE, 0x1000);
    create_unimplemented_device("esp32.uart1", DR_REG_UART1_BASE, 0x1000);
    create_unimplemented_device("esp32.tg0", DR_REG_TIMERGROUP0_BASE, 0x1000);
    create_unimplemented_device("esp32.efuse", DR_REG_EFUSE_BASE, 0x1000);
    create_unimplemented_device("esp32.iomux", DR_REG_IO_MUX_BASE, 0x2000);
    create_unimplemented_device("esp32.hinf", DR_REG_HINF_BASE, 0x1000);
    create_unimplemented_device("esp32.slc", DR_REG_SLC_BASE, 0x1000);
    create_unimplemented_device("esp32.slchost", DR_REG_SLCHOST_BASE, 0x1000);

}

static void esp32_soc_init(Object *obj)
{
    Esp32SocState *s = ESP32_SOC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    object_initialize_child(obj, "cpu", &s->cpu, sizeof(s->cpu), TYPE_ESP32_CPU, &error_abort, NULL);

    object_initialize_child(obj, "uart", &s->uart, sizeof(s->uart),
                            TYPE_ESP32_UART, &error_abort, NULL);

    object_property_add_alias(obj, "serial0", OBJECT(&s->uart), "chardev",
                              &error_abort);

    object_initialize_child(obj, "gpio", &s->gpio, sizeof(s->gpio),
                                TYPE_ESP32_GPIO, &error_abort, NULL);
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

    cpu_reset(CPU(&s->cpu));
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
                               translate_phys_addr, &s->cpu,
                               &elf_entry, &elf_lowaddr,
                               NULL, 0, EM_XTENSA, 0, 0);
        if (success > 0) {
            s->cpu.env.pc = elf_entry;
        }
    }
}

/* Initialize machine type */
static void esp32_machine_init(MachineClass *mc)
{
    mc->desc = "Espressif ESP32 machine";
    mc->init = esp32_machine_inst_init;
}

DEFINE_MACHINE("esp32", esp32_machine_init)


/*
 * ESP32-C6 SoC and machine
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/riscv/riscv_hart.h"
#include "target/riscv/esp_cpu.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"
#include "net/net.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/misc/esp32c6_reg.h"
#include "hw/misc/esp32c6_cache.h"
#include "hw/misc/esp32c6_pcr.h"
#include "hw/misc/esp32c6_lp.h"
#include "hw/misc/esp32c6_spi_mem.h"
#include "hw/misc/esp32c6_i2c_ana_mst.h"
#include "hw/misc/esp32c6_jtag.h"
#include "hw/misc/esp32c6_modem.h"
#include "hw/misc/unimp.h"
#include "hw/char/esp32c6_uart.h"
#include "hw/gpio/esp32c6_gpio.h"
#include "hw/nvram/esp32c6_efuse.h"
#include "hw/riscv/esp32c6_clk.h"
#include "hw/riscv/esp32c6_intmatrix.h"
#include "hw/misc/esp32c6_intpri.h"
#include "hw/misc/esp32c6_sha.h"
#include "hw/timer/esp32c6_timg.h"
#include "hw/timer/esp32c6_systimer.h"
#include "hw/ssi/esp32c6_spi.h"
#include "hw/dma/esp32c6_gdma.h"

#define ESP32C6_RESET_ADDRESS       0x40000000
#define ESP32C6_RESET_GPIO_NAME     "esp32c6.machine.reset_gpio"
#define MB (1024*1024)

#define ESP32C6_IRAM_SIZE           (512 * 1024)
#define ESP32C6_RTC_SIZE            (16 * 1024)
#define ESP32C6_IROM_SIZE           (320 * 1024)

struct Esp32C6MachineState {
    MachineState parent;

    EspRISCVCPU soc;
    BusState periph_bus;

    qemu_irq cpu_reset;

    DeviceState *eth;
    ESP32C6IntMatrixState intmatrix;
    ESP32C6IntpriState intpri;
    ESP32C6UARTState uart[ESP32C6_UART_COUNT];
    ESP32C6GPIOState gpio;
    ESP32C6CacheState cache;
    ESP32C6EfuseState efuse;
    ESP32C6ClockState clock;
    ESP32C6TimgState timg[2];
    ESP32C6SysTimerState systimer;
    ESP32C6SpiState spi1;
    ESP32C6GdmaState gdma;
    ESP32C6ShaState sha;
    ESP32C6UsbJtagState jtag;
    ESP32C6PcrState pcr;
    ESP32C6LpState lp;
    ESP32C6SpiMemState spi_mem;
    ESP32C6I2cAnaMstState i2c_ana_mst;
    ESP32C6ModemState modem;
};

#define TYPE_ESP32C6_MACHINE MACHINE_TYPE_NAME("esp32c6")
OBJECT_DECLARE_SIMPLE_TYPE(Esp32C6MachineState, ESP32C6_MACHINE)

enum MemoryRegions {
    ESP32C6_MEMREGION_IROM,
    ESP32C6_MEMREGION_DRAM,
    ESP32C6_MEMREGION_IRAM,
    ESP32C6_MEMREGION_RTCFAST,
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32c6_memmap[] = {
    [ESP32C6_MEMREGION_IROM]    = { 0x40000000,  ESP32C6_IROM_SIZE },
    [ESP32C6_MEMREGION_DRAM]    = { 0x40800000,  ESP32C6_IRAM_SIZE },
    [ESP32C6_MEMREGION_IRAM]    = { 0x40800000,  ESP32C6_IRAM_SIZE },
    [ESP32C6_MEMREGION_RTCFAST] = { 0x50000000,  ESP32C6_RTC_SIZE },
};

static void esp32c6_reset_request(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void esp32c6_init_spi_flash(Esp32C6MachineState *ms, BlockBackend *blk)
{
    DeviceState *spi_master = DEVICE(&ms->spi1);
    BusState *spi_bus = qdev_get_child_bus(spi_master, "spi");
    const char *flash_model = NULL;
    int64_t image_size = blk_getlength(blk);

    switch (image_size) {
        case 2 * MB:
            flash_model = "w25x16";
            break;
        case 4 * MB:
            flash_model = "gd25q32";
            break;
        case 8 * MB:
            flash_model = "gd25q64";
            break;
        case 16 * MB:
            flash_model = "is25lp128";
            break;
        default:
            error_report("Drive size error: only 2, 4, 8, and 16MB images are supported");
            return;
    }

    DeviceState *flash_dev = qdev_new(flash_model);
    qdev_prop_set_drive(flash_dev, "drive", blk);
    qdev_prop_set_uint8(flash_dev, "cs", 1);
    qdev_realize(flash_dev, spi_bus, &error_fatal);
    qdev_connect_gpio_out_named(spi_master, SSI_GPIO_CS, 0,
                                qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0));
}

static void esp32c6_init_openeth(Esp32C6MachineState *ms)
{
    MemoryRegion *sys_mem = get_system_memory();

    DeviceState *open_eth_dev = qemu_create_nic_device("open_eth", true, NULL);
    if (!open_eth_dev) {
        return;
    }

    ms->eth = open_eth_dev;

    SysBusDevice *sbd = SYS_BUS_DEVICE(open_eth_dev);
    sysbus_realize(sbd, &error_fatal);

    MemoryRegion *mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EMAC_BASE, mr, 0);
    mr = sysbus_mmio_get_region(sbd, 1);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EMAC_BASE + 0x400, mr, 0);

    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(&ms->intmatrix), 0 /* WIFI_MAC / ETH_MAC */));
}

static void esp32c6_load_firmware(MachineState *machine)
{
    Esp32C6MachineState *ms = ESP32C6_MACHINE(machine);
    const char *bios_filename = NULL;

    if (machine->firmware) {
        bios_filename = machine->firmware;
    }

    if (machine->kernel_filename) {
        if (bios_filename) {
            qemu_log("Warning: both -bios and -kernel specified. Loading -kernel file.\n");
        }
        bios_filename = machine->kernel_filename;
    }

    if (bios_filename) {
        RISCVHartArrayState hart = {
            .harts = &ms->soc.parent_obj,
            .num_harts = 1,
        };

        uint64_t elf_entry = ESP32C6_RESET_ADDRESS;
        int load_result = load_elf_ram_sym(bios_filename, NULL, NULL, NULL,
                        &elf_entry, NULL, NULL, NULL, 0,
                        EM_RISCV, 1, 0, NULL, false, NULL);

        if (load_result > 0) {
            qemu_log("Loading kernel at address 0x%08" PRIx64 "\n", elf_entry);
            riscv_load_kernel(machine, &hart, elf_entry, false, NULL);
            if (elf_entry != ESP32C6_RESET_ADDRESS) {
                qdev_prop_set_uint64(DEVICE(&ms->soc), "resetvec", elf_entry);
            }
        } else {
            int size = load_image_targphys_as(bios_filename, ESP32C6_RESET_ADDRESS,
                                             ESP32C6_IROM_SIZE, &address_space_memory);
            if (size < 0) {
                error_report("Error: could not load ROM/bios binary '%s' (max %u bytes)",
                             bios_filename, (unsigned)ESP32C6_IROM_SIZE);
                exit(1);
            }
        }
    } else {
        char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32c6-rom.bin");
        if (rom_binary == NULL) {
            error_report("Error: -bios argument not set, and esp32c6-rom.bin not found");
            exit(1);
        }

        int size = load_image_targphys_as(rom_binary, ESP32C6_RESET_ADDRESS,
                                         ESP32C6_IROM_SIZE, CPU(&ms->soc)->as);
        if (size < 0) {
            error_report("Error: could not load ROM binary '%s'", rom_binary);
            exit(1);
        }

        g_free(rom_binary);
    }
}

static void esp32c6_machine_init(MachineState *machine)
{
    BlockBackend *blk = NULL;
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qemu_log("Adding SPI flash device\n");
        blk = blk_by_legacy_dinfo(dinfo);
    } else {
        qemu_log("Not initializing SPI Flash\n");
    }

    Esp32C6MachineState *ms = ESP32C6_MACHINE(machine);

    object_initialize_child(OBJECT(ms), "soc", &ms->soc, TYPE_ESP_RISCV_CPU);
    qdev_prop_set_uint64(DEVICE(&ms->soc), "resetvec", ESP32C6_RESET_ADDRESS);
    /* The ESP32-C6 implements the Espressif PMA CSR extension */
    qdev_prop_set_bit(DEVICE(&ms->soc), "has-pma", true);
    /* The ESP32-C6 repurposes mie (0x304) as a per-line external-interrupt
     * enable bitmap (MXIE) per TRM §1.5.2 Reg 1.8 / §1.6.2; the intmatrix
     * uses cpu->mie_enabled as a second per-line gate alongside
     * PLIC_MXINT_ENABLE_REG.  Required for TEE-flavoured IDF builds that
     * mask/unmask interrupts via RV_SET_CSR(mie, …). */
    qdev_prop_set_bit(DEVICE(&ms->soc), "mie-as-bitmap", true);

    const struct MemmapEntry *memmap = esp32c6_memmap;
    MemoryRegion *sys_mem = get_system_memory();

    MemoryRegion *irom = g_new(MemoryRegion, 1);
    memory_region_init_rom(irom, NULL, "esp32c6.irom",
                          memmap[ESP32C6_MEMREGION_IROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C6_MEMREGION_IROM].base, irom);

    MemoryRegion *iram = g_new(MemoryRegion, 1);
    memory_region_init_ram(iram, NULL, "esp32c6.iram",
                          memmap[ESP32C6_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C6_MEMREGION_IRAM].base, iram);

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    memory_region_init_alias(dram, NULL, "esp32c6.dram", iram, 0,
                             memmap[ESP32C6_MEMREGION_DRAM].size);
    memory_region_add_subregion(sys_mem, memmap[ESP32C6_MEMREGION_DRAM].base, dram);

    MemoryRegion *rtcram = g_new(MemoryRegion, 1);
    memory_region_init_ram(rtcram, NULL, "esp32c6.rtcram",
                          memmap[ESP32C6_MEMREGION_RTCFAST].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C6_MEMREGION_RTCFAST].base, rtcram);

    esp32c6_load_firmware(machine);

    qdev_realize(DEVICE(&ms->soc), NULL, &error_fatal);

    qbus_init(&ms->periph_bus, sizeof(ms->periph_bus),
              TYPE_SYSTEM_BUS, DEVICE(&ms->soc), "esp32c6-periph-bus");

    qdev_init_gpio_in_named(DEVICE(&ms->soc), esp32c6_reset_request,
                           ESP32C6_RESET_GPIO_NAME, 1);

    /* Initialize child objects */
    for (int i = 0; i < ESP32C6_UART_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "uart%d", i);
        object_initialize_child(OBJECT(machine), name, &ms->uart[i], TYPE_ESP32C6_UART);
        snprintf(name, sizeof(name), "serial%d", i);
        object_property_add_alias(OBJECT(machine), name, OBJECT(&ms->uart[i]), "chardev");
        qdev_prop_set_chr(DEVICE(&ms->uart[i]), "chardev", serial_hd(i));
    }

    object_initialize_child(OBJECT(machine), "intmatrix", &ms->intmatrix, TYPE_ESP32C6_INTMATRIX);
    object_initialize_child(OBJECT(machine), "intpri", &ms->intpri, TYPE_ESP32C6_INTPRI);
    object_initialize_child(OBJECT(machine), "gpio", &ms->gpio, TYPE_ESP32C6_GPIO);
    object_initialize_child(OBJECT(machine), "extmem", &ms->cache, TYPE_ESP32C6_CACHE);
    object_initialize_child(OBJECT(machine), "efuse", &ms->efuse, TYPE_ESP32C6_EFUSE);
    object_initialize_child(OBJECT(machine), "clock", &ms->clock, TYPE_ESP32C6_CLOCK);
    object_initialize_child(OBJECT(machine), "gdma", &ms->gdma, TYPE_ESP32C6_GDMA);
    object_initialize_child(OBJECT(machine), "sha", &ms->sha, TYPE_ESP32C6_SHA);
    object_initialize_child(OBJECT(machine), "timg0", &ms->timg[0], TYPE_ESP32C6_TIMG);
    object_initialize_child(OBJECT(machine), "timg1", &ms->timg[1], TYPE_ESP32C6_TIMG);
    object_initialize_child(OBJECT(machine), "systimer", &ms->systimer, TYPE_ESP32C6_SYSTIMER);
    object_initialize_child(OBJECT(machine), "spi1", &ms->spi1, TYPE_ESP32C6_SPI);
    object_initialize_child(OBJECT(machine), "jtag", &ms->jtag, TYPE_ESP32C6_JTAG);
    object_initialize_child(OBJECT(machine), "pcr", &ms->pcr, TYPE_ESP32C6_PCR);
    object_initialize_child(OBJECT(machine), "lp", &ms->lp, TYPE_ESP32C6_LP);
    object_initialize_child(OBJECT(machine), "spi_mem", &ms->spi_mem, TYPE_ESP32C6_SPI_MEM);
    object_initialize_child(OBJECT(machine), "i2c_ana_mst", &ms->i2c_ana_mst, TYPE_ESP32C6_I2C_ANA_MST);
    object_initialize_child(OBJECT(machine), "modem_lpcon", &ms->modem, TYPE_ESP32C6_MODEM);

    /* Interrupt matrix + PLIC */
    DeviceState *intmatrix_dev = DEVICE(&ms->intmatrix);
    {
        object_property_set_link(OBJECT(&ms->intmatrix), "cpu", OBJECT(&ms->soc), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->intmatrix), &error_fatal);

        MemoryRegion *map_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTERRUPT_BASE, map_mr, 0);

        MemoryRegion *plic_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix), 1);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_PLIC_MX_BASE, plic_mr, 0);

        for (int i = 0; i <= ESP32C6_CPU_INT_COUNT; i++) {
            qemu_irq cpu_input = qdev_get_gpio_in_named(DEVICE(&ms->soc), ESP_CPU_IRQ_LINES_NAME, i);
            qdev_connect_gpio_out_named(intmatrix_dev, ESP32C6_INT_MATRIX_OUTPUT_NAME, i, cpu_input);
        }
    }

    esp32c6_init_openeth(ms);

    /* USB Serial JTAG — connect to serial_hd(2) to avoid doubling UART0 */
    {
        qdev_prop_set_chr(DEVICE(&ms->jtag), "chardev", serial_hd(ESP32C6_UART_COUNT));
        sysbus_realize(SYS_BUS_DEVICE(&ms->jtag), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->jtag), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_USB_SERIAL_JTAG_BASE, mr, 0);
    }

    /* SPI1 (Flash) */
    {
        ms->spi1.parent.xts_aes = NULL;
        sysbus_realize(SYS_BUS_DEVICE(&ms->spi1), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi1), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI1_BASE, mr, 0);
        if (blk) {
            esp32c6_init_spi_flash(ms, blk);
        }
    }

    /* UARTs */
    for (int i = 0; i < ESP32C6_UART_COUNT; ++i) {
        const hwaddr uart_base[] = { DR_REG_UART_BASE, DR_REG_UART1_BASE };
        sysbus_realize(SYS_BUS_DEVICE(&ms->uart[i]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->uart[i]), 0);
        memory_region_add_subregion_overlap(sys_mem, uart_base[i], mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->uart[i]), 0,
                           qdev_get_gpio_in(intmatrix_dev, C6_ETS_UART0_INTR_SOURCE + i));
    }

    /* GPIO */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->gpio), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gpio), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, mr, 0);
    }

    /* Cache (EXTMEM) */
    {
        if (blk) {
            ms->cache.flash_blk = blk;
        }
        sysbus_realize(SYS_BUS_DEVICE(&ms->cache), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->cache), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EXTMEM_BASE, mr, 0);
        memory_region_add_subregion_overlap(sys_mem, ms->cache.cache_base, &ms->cache.cache, 0);
    }

    /* eFuse */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->efuse), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->efuse), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EFUSE_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->efuse), 0,
                           qdev_get_gpio_in(intmatrix_dev, C6_ETS_EFUSE_INTR_SOURCE));
    }

    /* PCR (Peripheral Clock and Reset) */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->pcr), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->pcr), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSCON_BASE, mr, 0);
    }

    /* LP domain (LP_CLKRST, LP_AON, LP_TIMER, RTC_I2C) */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->lp), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->lp), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C6_LP_BASE, mr, 0);
        qdev_connect_gpio_out_named(DEVICE(&ms->lp), ESP32C6_LP_RESET_GPIO, 0,
                                    qdev_get_gpio_in_named(DEVICE(&ms->soc),
                                        ESP32C6_RESET_GPIO_NAME, 0));
    }

    /* SPI0 MEM controller (MMU, timing calibration) */
    {
        if (blk) {
            ms->spi_mem.flash_blk = blk;
        }
        ms->spi_mem.dcache_mr = &ms->cache.cache;
        sysbus_realize(SYS_BUS_DEVICE(&ms->spi_mem), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi_mem), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C6_SPI_MEM_BASE, mr, 0);
    }

    /* I2C Analog Master */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->i2c_ana_mst), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->i2c_ana_mst), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C6_I2C_ANA_MST_BASE, mr, 0);
    }

    /* GDMA */
    {
        object_property_set_link(OBJECT(&ms->gdma), "soc_mr", OBJECT(dram), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->gdma), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gdma), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GDMA_BASE, mr, 0);
        for (int i = 0; i < ESP32C6_GDMA_CHANNEL_COUNT; i++) {
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_IN_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, C6_ETS_DMA_IN_CH0_INTR_SOURCE + i));
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_OUT_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, C6_ETS_DMA_OUT_CH0_INTR_SOURCE + i));
        }
    }

    /* SHA */
    {
        ms->sha.parent.parent.gdma = ESP_GDMA(&ms->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ms->sha), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->sha), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SHA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->sha), 0,
                           qdev_get_gpio_in(intmatrix_dev, C6_ETS_SHA_INTR_SOURCE));
    }

    /* MODEM_LPCON (modem clock control) */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->modem), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->modem), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C6_MODEM_BASE, mr, 0);
    }

    /* System clock */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->clock), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->clock), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTEM_BASE, mr, 0);
    }

    /* INTPRI — software-triggered cross-core interrupts */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->intpri), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intpri), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTPRI_BASE, mr, 0);

        for (int i = 0; i < ESP32C6_INTPRI_CPU_INTR_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->intpri), i,
                               qdev_get_gpio_in(intmatrix_dev, C6_ETS_FROM_CPU_INTR0_SOURCE + i));
        }
    }

    /* Timer Groups */
    for (int tg = 0; tg < 2; tg++) {
        sysbus_realize(SYS_BUS_DEVICE(&ms->timg[tg]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->timg[tg]), 0);
        const hwaddr timg_base[] = { DR_REG_TIMERGROUP0_BASE, DR_REG_TIMERGROUP1_BASE };
        memory_region_add_subregion_overlap(sys_mem, timg_base[tg], mr, 0);

        const int tg0_src[] = { C6_ETS_TG0_T0_LEVEL_INTR_SOURCE, C6_ETS_TG0_WDT_LEVEL_INTR_SOURCE };
        const int tg1_src[] = { C6_ETS_TG1_T0_LEVEL_INTR_SOURCE, C6_ETS_TG1_WDT_LEVEL_INTR_SOURCE };
        const int *src = tg == 0 ? tg0_src : tg1_src;

        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32C6_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, src[0]));
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32C6_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, src[1]));

        const int wdt_reset_reason = tg == 0 ? ESP32C6_CORE_MWDT0 : ESP32C6_CORE_MWDT1;
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32C6_WDT_IRQ_RESET, 0,
                                    qdev_get_gpio_in(DEVICE(&ms->lp), wdt_reset_reason));
    }

    /* System timer */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->systimer), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->systimer), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTIMER_BASE, mr, 0);
        for (int i = 0; i < ESP_SYSTIMER_IRQ_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->systimer), i,
                               qdev_get_gpio_in(intmatrix_dev, C6_ETS_SYSTIMER_TARGET0_INTR_SOURCE + i));
        }
    }

    /* Catch-all for the entire peripheral address space (priority -1000).
     * Specific devices mapped above at priority 0 take precedence. */
    create_unimplemented_device("esp32c6.peripherals", ESP32C6_IO_START_ADDR, 0x100000);
    create_unimplemented_device("esp32c6.hp_peri", 0x20000000, 0x100000);
}

static void esp32c6_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Espressif ESP32-C6 machine";
    mc->default_cpu_type = TYPE_ESP_RISCV_CPU;
    mc->init = esp32c6_machine_init;
    mc->max_cpus = 1;
    mc->default_cpus = 1;
    mc->default_ram_size = 512 * 1024;
}

static const TypeInfo esp32c6_info = {
    .name = TYPE_ESP32C6_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Esp32C6MachineState),
    .class_init = esp32c6_machine_class_init,
};

static void esp32c6_machine_type_init(void)
{
    type_register_static(&esp32c6_info);
}

type_init(esp32c6_machine_type_init);

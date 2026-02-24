/*
 * ESP32-C3 SoC and machine
 *
 * Copyright (c) 2019-2022 Espressif Systems (Shanghai) Co. Ltd.
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
#include "hw/misc/esp32c3_reg.h"
#include "hw/misc/esp32c3_rtc_cntl.h"
#include "hw/misc/esp32c3_cache.h"
#include "hw/char/esp32c3_uart.h"
#include "hw/gpio/esp32c3_gpio.h"
#include "hw/nvram/esp32c3_efuse.h"
#include "hw/riscv/esp32c3_clk.h"
#include "hw/riscv/esp32c3_intmatrix.h"
#include "hw/misc/esp32c3_sha.h"
#include "hw/timer/esp32c3_timg.h"
#include "hw/timer/esp32c3_systimer.h"
#include "hw/ssi/esp32c3_spi.h"
#include "hw/ssi/esp32c3_spi2.h"
#include "hw/sd/sd.h"
#include "hw/misc/esp32c3_rtc_cntl.h"
#include "hw/misc/esp32c3_aes.h"
#include "hw/misc/esp32c3_rsa.h"
#include "hw/misc/esp32c3_hmac.h"
#include "hw/misc/esp32c3_ds.h"
#include "hw/misc/esp32c3_xts_aes.h"
#include "hw/misc/esp32c3_jtag.h"
#include "hw/dma/esp32c3_gdma.h"
#include "hw/display/esp_rgb.h"
#include "hw/net/can/esp32c3_twai.h"

#define ESP32C3_IO_WARNING          0

#define ESP32C3_RESET_ADDRESS       0x40000000
#define ESP32C3_RESET_GPIO_NAME     "esp32c3.machine.reset_gpio"
#define MB (1024*1024)


/* Define a new "class" which derivates from "MachineState" */
struct Esp32C3MachineState {
    MachineState parent;

    /* Attributes specific to our class */
    EspRISCVCPU soc;
    BusState periph_bus;
    MemoryRegion iomem;

    qemu_irq cpu_reset;

    DeviceState *eth; /* Ethernet controller */
    ESP32C3IntMatrixState intmatrix;
    ESP32C3UARTState uart[ESP32C3_UART_COUNT];
    ESP32C3GPIOState gpio;
    ESP32C3CacheState cache;
    ESP32C3EfuseState efuse;
    ESP32C3ClockState clock;
    ESP32C3GdmaState gdma;
    ESP32C3AesState aes;
    ESP32C3ShaState sha;
    ESP32C3RsaState rsa;
    ESP32C3HmacState hmac;
    ESP32C3DsState ds;
    ESP32C3XtsAesState xts_aes;
    ESP32C3TimgState timg[2];
    ESP32C3SysTimerState systimer;
    ESP32C3SpiState spi1;
    ESP32C3Spi2State spi2;
    ESP32C3RtcCntlState rtccntl;
    ESP32C3UsbJtagState jtag;
    ESPRgbState rgb;
    Esp32C3TWAIState twai;

    /* SD card ssi-sd device for GPIO CS routing */
    DeviceState *sd_ssi_dev;
};

/* Fake register used by ESP-IDF application to determine whether the code is running on real hardware or on QEMU */
#define A_SYSCON_ORIGIN_REG     0x3F8
/* Temporary macro for generating a random value from register SYSCON_RND_DATA_REG */
#define A_SYSCON_RND_DATA_REG   0x0B0

/* Temporary macro to mark the CPU as in non-debugging mode */
#define A_ASSIST_DEBUG_CORE_0_DEBUG_MODE_REG    0x098

/* Create a macro which defines the name of our new machine class */
#define TYPE_ESP32C3_MACHINE MACHINE_TYPE_NAME("esp32c3")

/* This will create a macro ESP32_MACHINE, which can be used to check and cast a generic MachineClass
 * to the specific class we defined above: Esp32C3MachineState. */
OBJECT_DECLARE_SIMPLE_TYPE(Esp32C3MachineState, ESP32C3_MACHINE)

/* Memory entries for ESP32-C3 */
enum MemoryRegions {
    ESP32C3_MEMREGION_IROM,
    ESP32C3_MEMREGION_DROM,
    ESP32C3_MEMREGION_DRAM,
    ESP32C3_MEMREGION_IRAM,
    ESP32C3_MEMREGION_RTCFAST,
    ESP32C3_MEMREGION_DCACHE,
    ESP32C3_MEMREGION_ICACHE,
    ESP32C3_MEMREGION_FRAMEBUF,
};

#define ESP32C3_INTERNAL_SRAM0_SIZE (16*1024)

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32c3_memmap[] = {
    [ESP32C3_MEMREGION_IROM]    = { 0x40000000,  0x60000 },
    [ESP32C3_MEMREGION_DROM]    = { 0x3ff00000,  0x20000 },
    [ESP32C3_MEMREGION_DRAM]    = { 0x3fc80000,  0x60000 },
    /* Merge SRAM0 and SRAM1 into a single entry */
    [ESP32C3_MEMREGION_IRAM]    = { 0x4037c000,  0x60000 + ESP32C3_INTERNAL_SRAM0_SIZE },
    [ESP32C3_MEMREGION_RTCFAST] = { 0x50000000,   0x2000 },
    [ESP32C3_MEMREGION_DCACHE]  = { 0x3c000000, 0x800000 },
    [ESP32C3_MEMREGION_ICACHE]  = { 0x42000000, 0x800000 },
    /* Virtual Framebuffer, used for the graphical interface */
    [ESP32C3_MEMREGION_FRAMEBUF] = { 0x20000000, ESP_RGB_MAX_VRAM_SIZE }
};


static bool addr_in_range(hwaddr addr, hwaddr start, hwaddr end)
{
    return addr >= start && addr < end;
}

static uint64_t esp32c3_io_read(void *opaque, hwaddr addr, unsigned int size)
{
    if (addr_in_range(addr + ESP32C3_IO_START_ADDR, DR_REG_RTC_I2C_BASE, DR_REG_RTC_I2C_BASE + 0x100)) {
        return (uint32_t) 0xffffff;
    } else if (addr + ESP32C3_IO_START_ADDR == DR_REG_SYSCON_BASE + A_SYSCON_ORIGIN_REG) {
        /* Return "QEMU" as a 32-bit value */
        return 0x51454d55;
    } else if (addr + ESP32C3_IO_START_ADDR == DR_REG_SYSCON_BASE + A_SYSCON_RND_DATA_REG) {
        /* Return a random 32-bit value */
        static bool init = false;
        if (!init) {
            srand(time(NULL));
            init = true;
        }
        return rand();
    } else if (addr + ESP32C3_IO_START_ADDR == DR_REG_ASSIST_DEBUG_BASE + A_ASSIST_DEBUG_CORE_0_DEBUG_MODE_REG) {
        return 0;
    } else {
#if ESP32C3_IO_WARNING
        warn_report("[ESP32-C3] Unsupported read to $%08lx\n", ESP32C3_IO_START_ADDR + addr);
#endif
    }
    return 0;
}

static void esp32c3_io_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
#if ESP32C3_IO_WARNING
        warn_report("[ESP32-C3] Unsupported write $%08lx = %08lx\n", ESP32C3_IO_START_ADDR + addr, value);
#endif
}


/* Define operations for I/OS */
static const MemoryRegionOps esp32c3_io_ops = {
    .read =  esp32c3_io_read,
    .write = esp32c3_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


/**
 * @brief Callback invoked when SoC's ESP32C3_RESET_GPIO_NAME pin is toggled
 */
static void esp32c3_reset_request(void* opaque, int n, int level)
{
    if (level) {
        ShutdownCause cause = SHUTDOWN_CAUSE_GUEST_RESET;
        qemu_system_reset_request(cause);
    }
}


static void esp32c3_init_spi_flash(Esp32C3MachineState *ms, BlockBackend* blk)
{
    DeviceState *spi_master = DEVICE(&ms->spi1);
    BusState* spi_bus = qdev_get_child_bus(spi_master, "spi");
    const char* flash_model = NULL;
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

    /* Create the SPI flash model */
    DeviceState *flash_dev = qdev_new(flash_model);
    qdev_prop_set_drive(flash_dev, "drive", blk);
    qdev_prop_set_uint8(flash_dev, "cs", 1);

    /* Realize the SPI flash, its "drive" (blk) property must already be set! */
    qdev_realize(flash_dev, spi_bus, &error_fatal);
    qdev_connect_gpio_out_named(spi_master, SSI_GPIO_CS, 0,
                                qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0));
}


static void esp32c3_init_openeth(Esp32C3MachineState *ms)
{
    MemoryRegion* mr = NULL;
    SysBusDevice* sbd = NULL;

    MemoryRegion* sys_mem = get_system_memory();

    /* Create a new OpenCores Ethernet component */
    DeviceState* open_eth_dev = qemu_create_nic_device("open_eth", true, NULL);
    if (!open_eth_dev) {
        return;
    }

    ms->eth = open_eth_dev;

    sbd = SYS_BUS_DEVICE(open_eth_dev);
    sysbus_realize(sbd, &error_fatal);

    /* OpenCores Ethernet has two memory regions: one for registers and one for descriptors,
        * we need to provide one I/O range for each of them */
    mr = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EMAC_BASE, mr, 0);
    mr = sysbus_mmio_get_region(sbd, 1);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EMAC_BASE + 0x400, mr, 0);

    sysbus_connect_irq(sbd, 0,
                        qdev_get_gpio_in(DEVICE(&ms->intmatrix), ETS_ETH_MAC_INTR_SOURCE));

}


static void esp32c3_load_firmware(MachineState *machine)
{
    Esp32C3MachineState *ms = ESP32C3_MACHINE(machine);
    const char *bios_filename = NULL;

    if (machine->firmware) {
        bios_filename = machine->firmware;
    }

    if (machine->kernel_filename) {
        if (bios_filename) {
            qemu_log("Warning: both -bios and -kernel arguments specified. Only loading the the -kernel file.\n");
        }
        bios_filename = machine->kernel_filename;
    }

    if (bios_filename) {
        uint64_t elf_entry = ESP32C3_RESET_ADDRESS;

        /* Try to load as ELF first */
        if (load_elf_ram_sym(bios_filename, NULL, NULL, NULL,
                        &elf_entry, NULL, NULL, NULL, 0,
                        EM_RISCV, 1, 0, NULL, true, NULL) > 0) {
            qemu_log("Loaded ELF '%s' at entry 0x%08" PRIx64 "\n", bios_filename, elf_entry);
        } else {
            /* Not an ELF, try as raw binary at reset address */
            int size = load_image_targphys_as(bios_filename, ESP32C3_RESET_ADDRESS,
                                              0x60000, CPU(&ms->soc)->as);
            if (size < 0) {
                error_report("Error: could not load firmware '%s'", bios_filename);
                exit(1);
            }
            qemu_log("Loaded raw binary '%s' (%d bytes) at 0x%08x\n",
                     bios_filename, size, ESP32C3_RESET_ADDRESS);
            elf_entry = ESP32C3_RESET_ADDRESS;
        }

        if (elf_entry != ESP32C3_RESET_ADDRESS) {
            qemu_log("Setting resetvec to 0x%08" PRIx64 "\n", elf_entry);
            qdev_prop_set_uint64(DEVICE(&ms->soc), "resetvec", elf_entry);
        } else {
            qemu_log("Not changing resetvec, elf_entry == ESP32C3_RESET_ADDRESS\n");
        }
    } else {
        /* Open and load the "bios", which is the ROM binary, also named "first stage bootloader" */
        char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32c3-rom.bin");
        if (rom_binary == NULL) {
            error_report("Error: -bios argument not set, and ROM code binary not found (1)");
            exit(1);
        }

        /* Load ROM file at the reset address */
        int size = load_image_targphys_as(rom_binary, ESP32C3_RESET_ADDRESS, 0x60000, CPU(&ms->soc)->as);
        if (size < 0) {
            error_report("Error: could not load ROM binary '%s'", rom_binary);
            exit(1);
        }

        g_free(rom_binary);
    }
}


static void esp32c3_machine_init(MachineState *machine)
{
    /* First thing to do is to check if a drive format and a file ahve been passed through the command line.
     * In fact, we will emulate the SPI flash if `if=mtd` was given. To know this, we will need to use the
     * Global API's function `driver_get`. */
    BlockBackend* blk = NULL;
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        /* MTD was given! We need to initialize and emulate SPI flash */
        qemu_log("Adding SPI flash device\n");
        blk = blk_by_legacy_dinfo(dinfo);
    } else {
        qemu_log("Not initializing SPI Flash\n");
    }

    /* Re-use the macro that checks and casts any generic/parent class to the real child instance */
    Esp32C3MachineState *ms = ESP32C3_MACHINE(machine);

    /* Initialize SoC */
    object_initialize_child(OBJECT(ms), "soc", &ms->soc, TYPE_ESP_RISCV_CPU);
    qdev_prop_set_uint64(DEVICE(&ms->soc), "resetvec", ESP32C3_RESET_ADDRESS);

    /* Initialize the memory mapping */
    const struct MemmapEntry *memmap = esp32c3_memmap;
    MemoryRegion *sys_mem = get_system_memory();

    /* Initialize the IROM */
    MemoryRegion *irom = g_new(MemoryRegion, 1);
    memory_region_init_rom(irom, NULL, "esp32c3.irom", memmap[ESP32C3_MEMREGION_IROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C3_MEMREGION_IROM].base, irom);

    /* Initialize the DROM as an alias to IROM. */
    MemoryRegion *drom = g_new(MemoryRegion, 1);
    const hwaddr offset_in_orig = 0x40000;
    memory_region_init_alias(drom, NULL, "esp32c3.drom", irom, offset_in_orig, memmap[ESP32C3_MEMREGION_DROM].size);
    memory_region_add_subregion(sys_mem, memmap[ESP32C3_MEMREGION_DROM].base, drom);

    /* Initialize the IRAM */
    MemoryRegion *iram = g_new(MemoryRegion, 1);
    memory_region_init_ram(iram, NULL, "esp32c3.iram", memmap[ESP32C3_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C3_MEMREGION_IRAM].base, iram);

    /* Initialize DRAM as an alias to IRAM (not including Internal SRAM 0) */
    MemoryRegion *dram = g_new(MemoryRegion, 1);
    /* DRAM mirrors IRAM for SRAM 1, skip the SRAM 0 area */
    memory_region_init_alias(dram, NULL, "esp32c3.dram", iram,
                             ESP32C3_INTERNAL_SRAM0_SIZE, memmap[ESP32C3_MEMREGION_DRAM].size);
    memory_region_add_subregion(sys_mem, memmap[ESP32C3_MEMREGION_DRAM].base, dram);

    /* Initialize RTC Fast Memory as regular RAM */
    MemoryRegion *rtcram = g_new(MemoryRegion, 1);
    memory_region_init_ram(rtcram, NULL, "esp32c3.rtcram", memmap[ESP32C3_MEMREGION_RTCFAST].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C3_MEMREGION_RTCFAST].base, rtcram);

    esp32c3_load_firmware(machine);

    qdev_realize(DEVICE(&ms->soc), NULL, &error_fatal);

    /* Initialize stack pointer to top of DRAM (required when loading ELF directly) */
    CPURISCVState *env = &ms->soc.parent_obj.env;
    /* DRAM ends at 0x3FCE0000, set SP slightly below that */
    env->gpr[2] = memmap[ESP32C3_MEMREGION_DRAM].base + memmap[ESP32C3_MEMREGION_DRAM].size - 16;
    qemu_log("Initialized SP to 0x%08" PRIx64 "\n", (uint64_t)env->gpr[2]);

    memory_region_init_io(&ms->iomem, OBJECT(&ms->soc), &esp32c3_io_ops,
                          NULL, "esp32c3.iomem", 0xd1000);
    memory_region_add_subregion(sys_mem, ESP32C3_IO_START_ADDR, &ms->iomem);


    /* Initialize the peripheral bus */
    qbus_init(&ms->periph_bus, sizeof(ms->periph_bus),
              TYPE_SYSTEM_BUS, DEVICE(&ms->soc), "esp32c3-periph-bus");

    /* Initialize the main I/O of the CPU that waits for "reset" requests */
    qdev_init_gpio_in_named(DEVICE(&ms->soc), esp32c3_reset_request, ESP32C3_RESET_GPIO_NAME, 1);

    /* Initialize the I/O peripherals */
    for (int i = 0; i < ESP32C3_UART_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "uart%d", i);
        object_initialize_child(OBJECT(machine), name, &ms->uart[i], TYPE_ESP32C3_UART);

        snprintf(name, sizeof(name), "serial%d", i);
        object_property_add_alias(OBJECT(machine), name, OBJECT(&ms->uart[i]), "chardev");
        qdev_prop_set_chr(DEVICE(&ms->uart[i]), "chardev", serial_hd(i));
    }

    object_initialize_child(OBJECT(machine), "intmatrix", &ms->intmatrix, TYPE_ESP32C3_INTMATRIX);
    object_initialize_child(OBJECT(machine), "gpio", &ms->gpio, TYPE_ESP32C3_GPIO);
    object_initialize_child(OBJECT(machine), "extmem", &ms->cache, TYPE_ESP32C3_CACHE);
    object_initialize_child(OBJECT(machine), "efuse", &ms->efuse, TYPE_ESP32C3_EFUSE);
    object_initialize_child(OBJECT(machine), "clock", &ms->clock, TYPE_ESP32C3_CLOCK);
    object_initialize_child(OBJECT(machine), "sha", &ms->sha, TYPE_ESP32C3_SHA);
    object_initialize_child(OBJECT(machine), "aes", &ms->aes, TYPE_ESP32C3_AES);
    object_initialize_child(OBJECT(machine), "gdma", &ms->gdma, TYPE_ESP32C3_GDMA);
    object_initialize_child(OBJECT(machine), "rsa", &ms->rsa, TYPE_ESP32C3_RSA);
    object_initialize_child(OBJECT(machine), "hmac", &ms->hmac, TYPE_ESP32C3_HMAC);
    object_initialize_child(OBJECT(machine), "ds", &ms->ds, TYPE_ESP32C3_DS);
    object_initialize_child(OBJECT(machine), "xts_aes", &ms->xts_aes, TYPE_ESP32C3_XTS_AES);
    object_initialize_child(OBJECT(machine), "timg0", &ms->timg[0], TYPE_ESP32C3_TIMG);
    object_initialize_child(OBJECT(machine), "timg1", &ms->timg[1], TYPE_ESP32C3_TIMG);
    object_initialize_child(OBJECT(machine), "systimer", &ms->systimer, TYPE_ESP32C3_SYSTIMER);
    object_initialize_child(OBJECT(machine), "spi1", &ms->spi1, TYPE_ESP32C3_SPI);
    object_initialize_child(OBJECT(machine), "spi2", &ms->spi2, TYPE_ESP32C3_SPI2);
    object_initialize_child(OBJECT(machine), "rtccntl", &ms->rtccntl, TYPE_ESP32C3_RTC_CNTL);
    object_initialize_child(OBJECT(machine), "jtag", &ms->jtag, TYPE_ESP32C3_JTAG);
    object_initialize_child(OBJECT(machine), "rgb", &ms->rgb, TYPE_ESP_RGB);
    object_initialize_child(OBJECT(machine), "twai", &ms->twai, TYPE_ESP32C3_TWAI);

    /* Realize all the I/O peripherals we depend on */

    /* Interrupt matrix realization */
    DeviceState* intmatrix_dev = DEVICE(&ms->intmatrix);
    {
        /* Store the current Machine CPU in the interrupt matrix */
        object_property_set_link(OBJECT(&ms->intmatrix), "cpu", OBJECT(&ms->soc), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->intmatrix), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTERRUPT_BASE, mr, 0);

        /* Connect all the interrupt matrix 31 output lines to the CPU 31 input IRQ lines.
         * The lines are indexed starting at 1.
         */
        for (int i = 0; i <= ESP32C3_CPU_INT_COUNT; i++) {
            qemu_irq cpu_input = qdev_get_gpio_in_named(DEVICE(&ms->soc), ESP_CPU_IRQ_LINES_NAME, i);
            qdev_connect_gpio_out_named(intmatrix_dev, ESP32C3_INT_MATRIX_OUTPUT_NAME, i, cpu_input);
        }
    }

    /* Initialize OpenCores Ethernet controller now sicne it requires the interrupt matrix */
    esp32c3_init_openeth(ms);

    /* USB Serial JTAG realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->jtag), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->jtag), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_USB_SERIAL_JTAG_BASE, mr, 0);
    }

    /* RTC CNTL realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->rtccntl), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->rtccntl), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_RTCCNTL_BASE, mr, 0);
        /* Connect CNTL's reset-request GPIO to the SoC's reset GPIO */
        qdev_connect_gpio_out_named(DEVICE(&ms->rtccntl), ESP32C3_RTC_CPU_RESET_GPIO, 0,
                                    qdev_get_gpio_in_named(DEVICE(&ms->soc), ESP32C3_RESET_GPIO_NAME, 0));
    }

    /* SPI1 controller (SPI Flash) */
    {
        ms->spi1.xts_aes = &ms->xts_aes;
        sysbus_realize(SYS_BUS_DEVICE(&ms->spi1), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi1), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI1_BASE, mr, 0);
        if (blk) {
            esp32c3_init_spi_flash(ms, blk);
        }
    }

    /* SPI2 controller (GPSPI2 - General Purpose SPI for SD card) */
    {
        /* Link SPI2 to GDMA for DMA-mode transfers */
        ms->spi2.gdma = ESP_GDMA(&ms->gdma);

        sysbus_realize(SYS_BUS_DEVICE(&ms->spi2), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi2), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI2_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->spi2), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_SPI2_INTR_SOURCE));

        /* Attach SD card if if=sd,index=1 drive is specified.
         * Use -drive file=xxx.img,if=sd,index=1,format=raw to attach an SD image.
         * We use index=1 (unit=1) to avoid conflict with QEMU's default SD drive at unit=0. */
        DriveInfo *sd_dinfo = drive_get(IF_SD, 0, 1);
        fprintf(stderr, "Checking for SD card drive: %p\n", sd_dinfo);
        if (sd_dinfo) {
            DeviceState *card_dev;
            BusState *spi_bus = qdev_get_child_bus(DEVICE(&ms->spi2), "spi");

            qemu_log("Adding SD card device to SPI2\n");

            /* Create ssi-sd bridge which connects SPI bus to SD card */
            DeviceState *ssi_sd = qdev_new("ssi-sd");
            qdev_realize_and_unref(ssi_sd, spi_bus, &error_fatal);

            /* Create the SD card (SPI variant) and attach to ssi-sd */
            card_dev = qdev_new(TYPE_SD_CARD_SPI);
            qdev_prop_set_drive_err(card_dev, "drive", blk_by_legacy_dinfo(sd_dinfo), &error_fatal);
            /* Enable relaxed SPI mode for ESP-IDF SDSPI driver compatibility */
            qdev_prop_set_bit(card_dev, "spi-relaxed-mode", true);
            qdev_realize_and_unref(card_dev,
                                   qdev_get_child_bus(ssi_sd, "sd-bus"),
                                   &error_fatal);

            /* Connect CS line from SPI2 hardware CS to ssi-sd (fallback) */
            qdev_connect_gpio_out_named(DEVICE(&ms->spi2), SSI_GPIO_CS, 0,
                                        qdev_get_gpio_in_named(ssi_sd, SSI_GPIO_CS, 0));

            /* Store ssi_sd reference for GPIO CS connection after GPIO is realized */
            ms->sd_ssi_dev = ssi_sd;
        }
    }

    for (int i = 0; i < ESP32C3_UART_COUNT; ++i) {
        const hwaddr uart_base[] = { DR_REG_UART_BASE, DR_REG_UART1_BASE };
        sysbus_realize(SYS_BUS_DEVICE(&ms->uart[i]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->uart[i]), 0);
        memory_region_add_subregion_overlap(sys_mem, uart_base[i], mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->uart[i]), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_UART0_INTR_SOURCE + i));
    }

    /* GPIO realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->gpio), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gpio), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, mr, 0);

        /* Connect GPIO 10 to SD card CS (ssi-sd device) if SD card is attached.
         * The ESP-IDF SDSPI driver controls CS via GPIO, not hardware SPI CS.
         * GPIO 10 is the CS pin used by the OROBLANCO_ONE_C3 board in QEMU mode. */
        if (ms->sd_ssi_dev) {
            qemu_log("Connecting GPIO 10 to SD card CS\n");
            qdev_connect_gpio_out_named(DEVICE(&ms->gpio), ESP32C3_GPIO_OUT_IRQ, 10,
                                        qdev_get_gpio_in_named(ms->sd_ssi_dev, SSI_GPIO_CS, 0));
        }
    }

    /* (Extmem) Cache realization */
    {
        if (blk) {
            ms->cache.flash_blk = blk;
        }
        ms->cache.xts_aes = &ms->xts_aes;
        sysbus_realize(SYS_BUS_DEVICE(&ms->cache), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->cache), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EXTMEM_BASE, mr, 0);

        memory_region_add_subregion_overlap(sys_mem, ms->cache.dcache_base, &ms->cache.dcache, 0);
        memory_region_add_subregion_overlap(sys_mem, ms->cache.icache_base, &ms->cache.icache, 0);
    }

    /* eFuses realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->efuse), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->efuse), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EFUSE_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->efuse), 0,
                       qdev_get_gpio_in(intmatrix_dev, ETS_EFUSE_INTR_SOURCE));
    }

    /* System clock realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->clock), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->clock), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTEM_BASE, mr, 0);
        /* Connect the IRQ lines to the interrupt matrix */
        for (int i = 0; i < ESP32C3_SYSTEM_CPU_INTR_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->clock), i,
                           qdev_get_gpio_in(intmatrix_dev, ETS_FROM_CPU_INTR0_SOURCE + i));
        }
    }

    /* Timer Groups realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->timg[0]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->timg[0]), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TIMERGROUP0_BASE, mr, 0);
        /* Connect the T0 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[0]), ESP32C3_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_T0_LEVEL_INTR_SOURCE));
        /* Connect the Watchdog interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[0]), ESP32C3_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_WDT_LEVEL_INTR_SOURCE));
        /* Connect the Watchdog reset request to the CNTL's WDT0 line */
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[0]), ESP32C3_WDT_IRQ_RESET, 0,
                                    qdev_get_gpio_in(DEVICE(&ms->rtccntl), ESP32C3_TG0WDT_SYS_RESET));

    }
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->timg[1]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->timg[1]), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TIMERGROUP1_BASE, mr, 0);
        /* Connect the T0 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[1]), ESP32C3_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_T0_LEVEL_INTR_SOURCE));
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[1]), ESP32C3_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_WDT_LEVEL_INTR_SOURCE));
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[1]), ESP32C3_WDT_IRQ_RESET, 0,
                                    qdev_get_gpio_in(DEVICE(&ms->rtccntl), ESP32C3_TG1WDT_SYS_RESET));
    }

    /* System timer */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->systimer), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->systimer), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTIMER_BASE, mr, 0);
        for (int i = 0; i < ESP_SYSTIMER_IRQ_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->systimer), i,
                           qdev_get_gpio_in(intmatrix_dev, ETS_SYSTIMER_TARGET0_EDGE_INTR_SOURCE + i));
        }
    }

    /* GDMA Realization */
    {
        object_property_set_link(OBJECT(&ms->gdma), "soc_mr", OBJECT(dram), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->gdma), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gdma), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GDMA_BASE, mr, 0);
        /* On the ESP32-C3, both IN and OUT channels are connected to the same Connect the IRQs to the Interrupt Matrix */
        for (int i = 0; i < ESP32C3_GDMA_CHANNEL_COUNT; i++) {
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_IN_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, ETS_DMA_CH0_INTR_SOURCE + i));
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_OUT_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, ETS_DMA_CH0_INTR_SOURCE + i));
        }

    }

    /* SHA realization */
    {
        ms->sha.parent.gdma = ESP_GDMA(&ms->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ms->sha), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->sha), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SHA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->sha), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_SHA_INTR_SOURCE));
    }

    /* AES realization */
    {
        ms->aes.parent.gdma = ESP_GDMA(&ms->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ms->aes), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->aes), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_AES_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->aes), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_AES_INTR_SOURCE));
    }

    /* RSA realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->rsa), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->rsa), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_RSA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->rsa), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_RSA_INTR_SOURCE));
    }

    /* HMAC realization */
    {
        ms->hmac.parent.efuse = ESP_EFUSE(&ms->efuse);
        qdev_realize(DEVICE(&ms->hmac), &ms->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->hmac), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_HMAC_BASE, mr, 0);
    }

    /* Digital Signature realization */
    {
        ms->ds.parent.hmac = ESP_HMAC(&ms->hmac);
        ms->ds.parent.aes = ESP_AES(&ms->aes);
        ms->ds.parent.rsa = ESP_RSA(&ms->rsa);
        ms->ds.parent.sha = ESP_SHA(&ms->sha);
        qdev_realize(DEVICE(&ms->ds), &ms->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->ds), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_DIGITAL_SIGNATURE_BASE, mr, 0);
    }

    /* XTS-AES realization */
    {
        ms->xts_aes.efuse = ESP_EFUSE(&ms->efuse);
        ms->xts_aes.clock = &ms->clock;
        qdev_realize(DEVICE(&ms->xts_aes), &ms->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->xts_aes), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_AES_XTS_BASE, mr, 0);
    }

    /* RGB display realization */
    {
        /* Give the internal RAM memory region to the display */
        ms->rgb.intram = dram;
        sysbus_realize(SYS_BUS_DEVICE(&ms->rgb), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->rgb), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_FRAMEBUF_BASE, mr, 0);
        memory_region_add_subregion_overlap(sys_mem, esp32c3_memmap[ESP32C3_MEMREGION_FRAMEBUF].base, &ms->rgb.vram, 0);
    }

    /* TWAI peripheral realization */
    sysbus_realize(SYS_BUS_DEVICE(&ms->twai), &error_fatal);
    MemoryRegion *twai_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->twai), 0);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_TWAI_BASE, twai_mr, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&ms->twai), 0,
                       qdev_get_gpio_in(DEVICE(&ms->intmatrix), ETS_TWAI_INTR_SOURCE));
}


/* Initialize machine type */
static void esp32c3_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Espressif ESP32-C3 machine";
    mc->default_cpu_type = TYPE_ESP_RISCV_CPU;
    mc->init = esp32c3_machine_init;
    mc->max_cpus = 1;
    mc->default_cpus = 1;
    // 0x4f600
    mc->default_ram_size = 400 * 1024;
    /* Disable default devices that don't apply to embedded MCU.
     * We allow sdcard via -drive if=sd,index=1, but disable the default
     * empty SD card at index=0 to avoid conflicts. */
    mc->no_floppy = true;
    mc->no_cdrom = true;
    mc->no_parallel = true;
    mc->no_sdcard = true;  /* Disable default SD card - user must specify via -drive if=sd */
}

/* Create a new type of machine ("child class") */
static const TypeInfo esp32c3_info = {
    .name = TYPE_ESP32C3_MACHINE,
    /* Specify the parent class, i.e. the class we derivate from */
    .parent = TYPE_MACHINE,
    /* Real size in bytes of our machine instance */
    .instance_size = sizeof(Esp32C3MachineState),
    /* Override the init function to one we defined above */
    .class_init = esp32c3_machine_class_init,
};

static void esp32c3_machine_type_init(void)
{
    type_register_static(&esp32c3_info);
}

type_init(esp32c3_machine_type_init);

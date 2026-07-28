/*
 * ESP32-P4 SoC and machine
 *
 * Phase 1: single HP core0, CLIC intmatrix, boot-critical peripherals.
 * Dual-core / LP core / flash MMU are follow-ups (see reference.md).
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
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/misc/esp32p4_reg.h"
#include "hw/misc/esp32p4_cache.h"
#include "hw/misc/esp32p4_spi_mem.h"
#include "hw/misc/esp32p4_psram_mem.h"
#include "hw/misc/esp32p4_hp_sys_clkrst.h"
#include "hw/misc/esp32p4_lp.h"
#include "hw/misc/esp32p4_sha.h"
#include "hw/nvram/esp32p4_efuse.h"
#include "hw/misc/unimp.h"
#include "hw/char/esp32p4_uart.h"
#include "hw/gpio/esp32p4_gpio.h"
#include "hw/riscv/esp32p4_intmatrix.h"
#include "hw/timer/esp32p4_timg.h"
#include "hw/timer/esp32p4_systimer.h"
#include "hw/timer/esp_timg.h"
#include "hw/timer/esp_systimer.h"
#include "hw/ssi/esp32p4_spi.h"
#include "hw/ssi/ssi.h"

#define ESP32P4_RESET_ADDRESS       0x4FC00000
#define ESP32P4_RESET_GPIO_NAME     "esp32p4.machine.reset_gpio"

#define ESP32P4_IROM_SIZE           (128 * 1024)
#define ESP32P4_IRAM_SIZE           (768 * 1024)
#define ESP32P4_LP_RAM_SIZE         (32 * 1024)
#define ESP32P4_TCM_SIZE            (8 * 1024)

/*
 * ets_rom_layout_p, from esp32p4.rom.eco5.ld. The pointed-to ets_rom_layout_t
 * (rom_layout.h: 10 words for a dual-core SoC with SUPPORT_USB_DWCOTG) lives in
 * ROM .rodata; the ECO5 dump holds it at 0x4FC1DB38. NuttX and IDF dereference
 * the pointer to size the heap, so it must be populated even when the dump does
 * not reach the last word of the ROM window.
 */
#define ESP32P4_ETS_ROM_LAYOUT_PTR  0x4FC1FFFC
#define ESP32P4_ETS_ROM_LAYOUT      0x4FC1DB38

/*
 * ets_delay_us (0x4FC0003C per esp32p4.rom.eco5.ld) is only a `j` trampoline
 * into the real body at 0x4FC012F8:
 *
 *   csrr a4, mcycle
 *   lui  a5, 0x4ffc0
 *   lw   a5, -112(a5)      <- s_ticks_per_us, patched below
 *   mul  a0, a0, a5
 *   csrr a5, mcycle / sub / bltu   <- spin
 *
 * Replacing the load with an immediate keeps the spin loop intact while making
 * the delay independent of PLL calibration. Patching the trampoline instead
 * would turn ets_delay_us into a no-op (and clobber the neighbouring
 * ets_get_cpu_frequency entry), so any guest busy-wait would consume no
 * virtual time at all.
 */
#define ESP32P4_ETS_DELAY_US_TICKS  0x4FC01300

struct Esp32P4MachineState {
    MachineState parent;

    EspRISCVCPU soc;
    BusState periph_bus;

    ESP32P4IntMatrixState intmatrix;
    ESP32P4UARTState uart[ESP32P4_UART_COUNT];
    ESP32P4GPIOState gpio;
    ESP32P4TimgState timg[2];
    ESP32P4SysTimerState systimer;
    ESP32P4CacheState cache;
    ESP32P4SpiMemState spi_mem;
    ESP32P4PsramMemState psram_mem;
    ESP32P4SpiState spi1;
    ESP32P4HpSysClkrstState hp_sys_clkrst;
    ESP32P4LpState lp;
    ESP32P4ShaState sha;
    ESP32P4EfuseState efuse;
};

#define TYPE_ESP32P4_MACHINE MACHINE_TYPE_NAME("esp32p4")
OBJECT_DECLARE_SIMPLE_TYPE(Esp32P4MachineState, ESP32P4_MACHINE)

enum MemoryRegions {
    ESP32P4_MEMREGION_IROM,
    ESP32P4_MEMREGION_IRAM,
    ESP32P4_MEMREGION_LPRAM,
    ESP32P4_MEMREGION_TCM,
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32p4_memmap[] = {
    /* HP mask ROM — reset vector */
    [ESP32P4_MEMREGION_IROM]  = { 0x4FC00000, ESP32P4_IROM_SIZE },
    /* Internal SRAM (IRAM/DRAM shared view) */
    [ESP32P4_MEMREGION_IRAM]  = { 0x4FF00000, ESP32P4_IRAM_SIZE },
    /* LP fast RAM */
    [ESP32P4_MEMREGION_LPRAM] = { 0x50108000, ESP32P4_LP_RAM_SIZE },
    /* TCM */
    [ESP32P4_MEMREGION_TCM]   = { 0x30100000, ESP32P4_TCM_SIZE },
};

static void esp32p4_reset_request(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void esp32p4_init_spi_flash(Esp32P4MachineState *ms, BlockBackend *blk)
{
    DeviceState *spi_master = DEVICE(&ms->spi1);
    BusState *spi_bus = qdev_get_child_bus(spi_master, "spi");
    const char *flash_model = NULL;

    switch (blk_getlength(blk)) {
    case 2 * MiB:
        flash_model = "w25x16";
        break;
    case 4 * MiB:
        flash_model = "gd25q32";
        break;
    case 8 * MiB:
        flash_model = "gd25q64";
        break;
    case 16 * MiB:
        flash_model = "is25lp128";
        break;
    default:
        error_report("Failed to initialize SPI flash: unsupported size");
        return;
    }

    DeviceState *flash_dev = qdev_new(flash_model);
    qdev_prop_set_drive(flash_dev, "drive", blk);
    qdev_prop_set_uint8(flash_dev, "cs", 1);
    qdev_realize(flash_dev, spi_bus, &error_fatal);
    qdev_connect_gpio_out_named(spi_master, SSI_GPIO_CS, 0,
                                qdev_get_gpio_in_named(flash_dev,
                                                       SSI_GPIO_CS, 0));
}

static void esp32p4_write_boot_stub(hwaddr entry, hwaddr reset_addr)
{
    const uint32_t iram_top = 0x4FF00000 + ESP32P4_IRAM_SIZE;
    const uint32_t entry32 = (uint32_t)entry;

    uint32_t entry_lo = entry32 & 0xFFF;
    uint32_t entry_hi = entry32;
    if (entry_lo & 0x800) {
        entry_hi += 0x1000;
    }

    uint32_t stub[] = {
        (iram_top & 0xFFFFF000) | (2 << 7) | 0x37,          /* lui sp, iram_top */
        (0x50000000 & 0xFFFFF000) | (6 << 7) | 0x37,        /* lui a2, peri */
        (0x51 << 20) | (0 << 15) | (0 << 12) | (7 << 7) | 0x13,
        (0 << 25) | (7 << 20) | (6 << 15) | (2 << 12) | (0 << 7) | 0x23,
        (entry_hi & 0xFFFFF000) | (5 << 7) | 0x37,
        ((entry_lo & 0xFFF) << 20) | (5 << 15) | (0 << 12) | (5 << 7) | 0x13,
        (0x305 << 20) | (5 << 15) | (1 << 12) | (0 << 7) | 0x73,
        (5 << 15) | (0 << 12) | (0 << 7) | 0x67,
    };

    rom_add_blob_fixed("esp32p4.boot_stub", stub, sizeof(stub), reset_addr);
}

static void esp32p4_load_firmware(MachineState *machine)
{
    bool rom_loaded = false;

    if (machine->firmware) {
        uint64_t rom_entry = ESP32P4_RESET_ADDRESS;
        int r = load_elf_ram_sym(machine->firmware, NULL, NULL, NULL,
                                 &rom_entry, NULL, NULL, NULL, 0,
                                 EM_RISCV, 1, 0, NULL, false, NULL);
        if (r > 0) {
            qemu_log("Loaded ROM ELF, entry 0x%08" PRIx64 "\n", rom_entry);
            /*
             * ECO5: ets_get_bootloader_offset @ 0x4fc00a40 → return 0x2000
             * Stub both get/set with lui a0,2 ; ret
             */
            static const uint32_t rom_stubs[] = {
                0x00002537,  /* lui  a0, 2        (a0 = 0x2000) */
                0x00008067,  /* jalr zero, ra, 0  (ret)         */
            };
            rom_add_blob_fixed("esp32p4.rom_stubs",
                               rom_stubs, sizeof(rom_stubs), 0x4fc00a40);

            static const uint32_t rom_layout_ptr = ESP32P4_ETS_ROM_LAYOUT;
            rom_add_blob_fixed("esp32p4.rom_layout_ptr",
                               &rom_layout_ptr, sizeof(rom_layout_ptr),
                               ESP32P4_ETS_ROM_LAYOUT_PTR);

            rom_loaded = true;
        } else {
            int size = load_image_targphys_as(machine->firmware,
                                              ESP32P4_RESET_ADDRESS,
                                              ESP32P4_IROM_SIZE,
                                              &address_space_memory);
            if (size >= 0) {
                rom_loaded = true;
            } else {
                error_report("Error: could not load -bios '%s'",
                             machine->firmware);
                exit(1);
            }
        }
    }

    if (!rom_loaded) {
        char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                          "esp32p4-rom.bin");
        if (rom_binary != NULL) {
            ssize_t size = get_image_size(rom_binary);
            if (size > 0) {
                uint8_t *rom_buf = g_malloc(size);
                if (load_image_size(rom_binary, rom_buf, size) == size) {
                    const uint32_t delay_patch =
                        0x0A000793;  /* addi a5, zero, 160 */
                    const size_t patch_offset =
                        ESP32P4_ETS_DELAY_US_TICKS - ESP32P4_RESET_ADDRESS;
                    if (patch_offset + sizeof(delay_patch) <= (size_t)size) {
                        memcpy(rom_buf + patch_offset, &delay_patch,
                               sizeof(delay_patch));
                    }

                    rom_add_blob_fixed("esp32p4-rom.bin", rom_buf, size,
                                       ESP32P4_RESET_ADDRESS);

                    const hwaddr layout_ptr_off =
                        ESP32P4_ETS_ROM_LAYOUT_PTR - ESP32P4_RESET_ADDRESS;
                    if ((size_t)size <= layout_ptr_off) {
                        static const uint32_t rom_layout_ptr =
                            ESP32P4_ETS_ROM_LAYOUT;
                        rom_add_blob_fixed("esp32p4.rom_layout_ptr",
                                           &rom_layout_ptr,
                                           sizeof(rom_layout_ptr),
                                           ESP32P4_ETS_ROM_LAYOUT_PTR);
                    }

                    rom_loaded = true;
                }
                g_free(rom_buf);
            }
            g_free(rom_binary);
        }
    }

    if (machine->kernel_filename) {
        uint64_t elf_entry = 0;
        int r = load_elf_ram_sym(machine->kernel_filename, NULL, NULL, NULL,
                                 &elf_entry, NULL, NULL, NULL, 0,
                                 EM_RISCV, 1, 0, NULL, false, NULL);
        if (r > 0) {
            qemu_log("Loaded kernel ELF at 0x%08" PRIx64 "\n", elf_entry);
            if (!rom_loaded) {
                esp32p4_write_boot_stub(elf_entry, ESP32P4_RESET_ADDRESS);
                rom_loaded = true;
            }
        } else {
            error_report("Error: could not load kernel '%s'",
                         machine->kernel_filename);
            exit(1);
        }
    } else if (!rom_loaded) {
        error_report("Error: no ROM, -bios, or -kernel specified "
                     "(esp32p4-rom.bin not found)");
        exit(1);
    }
}

static void esp32p4_machine_init(MachineState *machine)
{
    Esp32P4MachineState *ms = ESP32P4_MACHINE(machine);
    const struct MemmapEntry *memmap = esp32p4_memmap;
    MemoryRegion *sys_mem = get_system_memory();
    BlockBackend *blk = NULL;

    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qemu_log("Adding SPI flash device\n");
        blk = blk_by_legacy_dinfo(dinfo);
    } else {
        qemu_log("Not initializing SPI Flash\n");
    }

    object_initialize_child(OBJECT(ms), "soc", &ms->soc, TYPE_ESP_RISCV_CPU);
    qdev_prop_set_uint64(DEVICE(&ms->soc), "resetvec", ESP32P4_RESET_ADDRESS);
    qdev_prop_set_bit(DEVICE(&ms->soc), "has-pma", true);
    qdev_prop_set_bit(DEVICE(&ms->soc), "has-pmp32", true);
    qdev_prop_set_bit(DEVICE(&ms->soc), "clic-ext-irq", true);

    {
        CPURISCVState *env = &ms->soc.parent_obj.env;
        env->misa_ext |= RVA | RVF;
        env->misa_ext_mask |= RVA | RVF;
        Object *soc_obj = OBJECT(&ms->soc);
        object_property_set_bool(soc_obj, "zca", true, &error_abort);
        object_property_set_bool(soc_obj, "zcb", true, &error_abort);
        object_property_set_bool(soc_obj, "zcmp", true, &error_abort);
        object_property_set_bool(soc_obj, "zcmt", true, &error_abort);
        object_property_set_bool(soc_obj, "zcd", false, &error_abort);
        object_property_set_bool(soc_obj, "zcf", true, &error_abort);
        object_property_set_bool(soc_obj, "zicsr", true, &error_abort);
    }

    MemoryRegion *irom = g_new(MemoryRegion, 1);
    memory_region_init_ram(irom, NULL, "esp32p4.irom",
                           memmap[ESP32P4_MEMREGION_IROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32P4_MEMREGION_IROM].base,
                                irom);

    MemoryRegion *iram = g_new(MemoryRegion, 1);
    memory_region_init_ram(iram, NULL, "esp32p4.iram",
                           memmap[ESP32P4_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32P4_MEMREGION_IRAM].base,
                                iram);

    MemoryRegion *lpram = g_new(MemoryRegion, 1);
    memory_region_init_ram(lpram, NULL, "esp32p4.lpram",
                           memmap[ESP32P4_MEMREGION_LPRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32P4_MEMREGION_LPRAM].base,
                                lpram);

    MemoryRegion *tcm = g_new(MemoryRegion, 1);
    memory_region_init_ram(tcm, NULL, "esp32p4.tcm",
                           memmap[ESP32P4_MEMREGION_TCM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32P4_MEMREGION_TCM].base,
                                tcm);

    /*
     * Cache controller owns the cached flash window and must win over the
     * esp32p4.cpu_peri catch-all for its register block.
     */
    object_initialize_child(OBJECT(machine), "cache", &ms->cache,
                            TYPE_ESP32P4_CACHE);
    sysbus_realize(SYS_BUS_DEVICE(&ms->cache), &error_fatal);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_CACHE_BASE,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->cache), 0), 0);
    memory_region_add_subregion_overlap(sys_mem, ESP32P4_FLASH_WINDOW_BASE,
                                        &ms->cache.flash_win, 0);

    esp32p4_load_firmware(machine);

    qdev_realize(DEVICE(&ms->soc), NULL, &error_fatal);

    qbus_init(&ms->periph_bus, sizeof(ms->periph_bus),
              TYPE_SYSTEM_BUS, DEVICE(&ms->soc), "esp32p4-periph-bus");

    qdev_init_gpio_in_named(DEVICE(&ms->soc), esp32p4_reset_request,
                            ESP32P4_RESET_GPIO_NAME, 1);

    for (int i = 0; i < ESP32P4_UART_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "uart%d", i);
        object_initialize_child(OBJECT(machine), name, &ms->uart[i],
                                TYPE_ESP32P4_UART);
        snprintf(name, sizeof(name), "serial%d", i);
        object_property_add_alias(OBJECT(machine), name,
                                  OBJECT(&ms->uart[i]), "chardev");
        qdev_prop_set_chr(DEVICE(&ms->uart[i]), "chardev", serial_hd(i));
    }

    object_initialize_child(OBJECT(machine), "intmatrix", &ms->intmatrix,
                            TYPE_ESP32P4_INTMATRIX);
    object_initialize_child(OBJECT(machine), "gpio", &ms->gpio,
                            TYPE_ESP32P4_GPIO);
    object_initialize_child(OBJECT(machine), "timg0", &ms->timg[0],
                            TYPE_ESP32P4_TIMG);
    object_initialize_child(OBJECT(machine), "timg1", &ms->timg[1],
                            TYPE_ESP32P4_TIMG);
    for (int i = 0; i < 2; i++) {
        ESPTimgState *timg = ESP_TIMG(&ms->timg[i]);
        timg->xtal_freq = ESP32P4_XTAL_FREQ_HZ;
        timg->wdt_clk_is_xtal = true;
        timg->cali_osc_freq[1] = 32768;
        timg->cali_osc_freq[2] = 32000;
        timg->cali_osc_freq[3] = 136000;
        timg->cali_osc_freq[4] = ESP_RC_FAST_FREQ / 256;
    }
    object_initialize_child(OBJECT(machine), "systimer", &ms->systimer,
                            TYPE_ESP32P4_SYSTIMER);
    object_initialize_child(OBJECT(machine), "spi_mem", &ms->spi_mem,
                            TYPE_ESP32P4_SPI_MEM);
    object_initialize_child(OBJECT(machine), "psram_mem", &ms->psram_mem,
                            TYPE_ESP32P4_PSRAM_MEM);
    object_initialize_child(OBJECT(machine), "spi1", &ms->spi1,
                            TYPE_ESP32P4_SPI);
    object_initialize_child(OBJECT(machine), "hp_sys_clkrst",
                            &ms->hp_sys_clkrst, TYPE_ESP32P4_HP_SYS_CLKRST);
    object_initialize_child(OBJECT(machine), "lp", &ms->lp, TYPE_ESP32P4_LP);
    object_initialize_child(OBJECT(machine), "sha", &ms->sha, TYPE_ESP32P4_SHA);
    object_initialize_child(OBJECT(machine), "efuse", &ms->efuse,
                            TYPE_ESP32P4_EFUSE);

    /* Interrupt matrix + CLIC (core0) */
    DeviceState *intmatrix_dev = DEVICE(&ms->intmatrix);
    {
        object_property_set_link(OBJECT(&ms->intmatrix), "cpu",
                                 OBJECT(&ms->soc), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->intmatrix), &error_fatal);

        MemoryRegion *map_mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTERRUPT_BASE,
                                            map_mr, 0);

        MemoryRegion *clic_mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix), 1);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_CLIC_BASE,
                                            clic_mr, 0);

        for (int i = 0; i <= ESP32P4_CPU_INT_COUNT; i++) {
            qemu_irq cpu_input = qdev_get_gpio_in_named(DEVICE(&ms->soc),
                                                        ESP_CPU_IRQ_LINES_NAME,
                                                        i);
            qdev_connect_gpio_out_named(intmatrix_dev,
                                        ESP32P4_INT_MATRIX_OUTPUT_NAME,
                                        i, cpu_input);
        }
    }

    /* UARTs */
    {
        const hwaddr uart_base[] = {
            DR_REG_UART0_BASE, DR_REG_UART1_BASE, DR_REG_UART2_BASE,
            DR_REG_UART3_BASE, DR_REG_UART4_BASE,
        };
        for (int i = 0; i < ESP32P4_UART_COUNT; ++i) {
            sysbus_realize(SYS_BUS_DEVICE(&ms->uart[i]), &error_fatal);
            MemoryRegion *mr =
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->uart[i]), 0);
            memory_region_add_subregion_overlap(sys_mem, uart_base[i], mr, 0);
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->uart[i]), 0,
                               qdev_get_gpio_in(intmatrix_dev,
                                                P4_ETS_UART0_INTR_SOURCE + i));
        }
    }

    /* GPIO + IO_MUX */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->gpio), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gpio), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, mr, 0);
        MemoryRegion *iomux =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gpio), 1);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_IO_MUX_BASE,
                                            iomux, 0);
    }

    /* SPI1 — flash access path used by the ROM and bootloader */
    {
        ms->spi1.parent.xts_aes = NULL;
        sysbus_realize(SYS_BUS_DEVICE(&ms->spi1), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi1), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI1_BASE, mr, 0);
        if (blk) {
            esp32p4_init_spi_flash(ms, blk);
        }
    }

    /* SPI0 MEM controller — flash MMU into the cached flash window */
    {
        ms->spi_mem.flash_blk = blk;
        ms->spi_mem.flash_win_mr = &ms->cache.flash_win;
        sysbus_realize(SYS_BUS_DEVICE(&ms->spi_mem), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi_mem), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_FLASH_SPI0_BASE,
                                            mr, 0);
    }

    /* PSRAM MSPI (SPI_MEM_S) — MMU regs + 64 MB cached PSRAM window */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->psram_mem), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->psram_mem), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_PSRAM_MSPI0_BASE,
                                            mr, 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32P4_PSRAM_WINDOW_BASE,
                                            &ms->psram_mem.psram, 0);
    }

    /* HP_SYS_CLKRST (PCR replacement) */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->hp_sys_clkrst), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->hp_sys_clkrst), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_HP_SYS_CLKRST_BASE,
                                            mr, 0);
    }

    /* LP domain */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->lp), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->lp), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32P4_LP_BASE, mr, 0);
        qdev_connect_gpio_out_named(DEVICE(&ms->lp), ESP32P4_LP_RESET_GPIO, 0,
                                    qdev_get_gpio_in_named(DEVICE(&ms->soc),
                                        ESP32P4_RESET_GPIO_NAME, 0));
    }

    /* eFuse — priority 1 to win over the LP domain catch-all */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->efuse), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->efuse), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EFUSE_BASE, mr, 1);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->efuse), 0,
                           qdev_get_gpio_in(intmatrix_dev,
                                            P4_ETS_LP_EFUSE_INTR_SOURCE));
    }

    /* SHA accelerator — CPU (M_MEM) mode only, no GDMA link */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->sha), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->sha), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SHA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->sha), 0,
                           qdev_get_gpio_in(intmatrix_dev,
                                            P4_ETS_SHA_INTR_SOURCE));
    }

    /* Timer Groups */
    for (int tg = 0; tg < 2; tg++) {
        sysbus_realize(SYS_BUS_DEVICE(&ms->timg[tg]), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->timg[tg]), 0);
        const hwaddr timg_base[] = {
            DR_REG_TIMERGROUP0_BASE, DR_REG_TIMERGROUP1_BASE
        };
        memory_region_add_subregion_overlap(sys_mem, timg_base[tg], mr, 0);

        const int tg0_src[] = {
            P4_ETS_TG0_T0_INTR_SOURCE, P4_ETS_TG0_T1_INTR_SOURCE,
            P4_ETS_TG0_WDT_LEVEL_INTR_SOURCE
        };
        const int tg1_src[] = {
            P4_ETS_TG1_T0_INTR_SOURCE, P4_ETS_TG1_T1_INTR_SOURCE,
            P4_ETS_TG1_WDT_LEVEL_INTR_SOURCE
        };
        const int *src = tg == 0 ? tg0_src : tg1_src;

        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]),
                                    ESP32P4_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, src[0]));
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]),
                                    ESP32P4_T1_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, src[1]));
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]),
                                    ESP32P4_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, src[2]));

        const int wdt_reset_reason =
            tg == 0 ? ESP32P4_CORE_MWDT0 : ESP32P4_CORE_MWDT1;
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]),
                                    ESP32P4_WDT_IRQ_RESET, 0,
                                    qdev_get_gpio_in(DEVICE(&ms->lp),
                                                     wdt_reset_reason));
    }

    /* System timer */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->systimer), &error_fatal);
        MemoryRegion *mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->systimer), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTIMER_BASE,
                                            mr, 0);
        for (int i = 0; i < ESP_SYSTIMER_IRQ_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->systimer), i,
                               qdev_get_gpio_in(intmatrix_dev,
                                   P4_ETS_SYSTIMER_TARGET0_INTR_SOURCE + i));
        }
    }

    create_unimplemented_device("esp32p4.peripherals",
                                ESP32P4_IO_START_ADDR, 0x100000);
    create_unimplemented_device("esp32p4.lp_rom", 0x50100000, 0x4000);
    create_unimplemented_device("esp32p4.clic_area", 0x20000000, 0x1000000);
    create_unimplemented_device("esp32p4.cpu_peri", 0x3FF00000, 0x20000);
}

static void esp32p4_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Espressif ESP32-P4 machine";
    mc->default_cpu_type = TYPE_ESP_RISCV_CPU;
    mc->init = esp32p4_machine_init;
    /* Dual-core deferred — see reference.md */
    mc->max_cpus = 1;
    mc->default_cpus = 1;
    mc->default_ram_size = ESP32P4_IRAM_SIZE;
}

static const TypeInfo esp32p4_info = {
    .name = TYPE_ESP32P4_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Esp32P4MachineState),
    .class_init = esp32p4_machine_class_init,
};

static void esp32p4_machine_type_init(void)
{
    type_register_static(&esp32p4_info);
}

type_init(esp32p4_machine_type_init)

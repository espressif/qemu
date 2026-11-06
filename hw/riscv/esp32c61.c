/*
 * ESP32-C61 SoC and machine
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
#include "hw/misc/esp32c61_reg.h"
#include "hw/misc/esp32c61_cache.h"
#include "hw/misc/esp32c61_pcr.h"
#include "hw/misc/esp32c61_lp.h"
#include "hw/misc/esp32c61_spi_mem.h"
#include "hw/misc/esp32c61_i2c_ana_mst.h"
#include "hw/misc/esp32c61_jtag.h"
#include "hw/misc/esp32c61_modem.h"
#include "hw/misc/unimp.h"
#include "hw/char/esp32c61_uart.h"
#include "hw/gpio/esp32c61_gpio.h"
#include "hw/nvram/esp32c61_efuse.h"
#include "hw/riscv/esp32c61_clk.h"
#include "hw/riscv/esp32c61_intmatrix.h"
#include "hw/misc/esp32c61_intpri.h"
#include "hw/misc/esp32c61_sha.h"
#include "hw/misc/esp_ecc.h"
#include "hw/misc/esp_ecdsa.h"
#include "hw/timer/esp32c61_timg.h"
#include "hw/timer/esp32c61_systimer.h"
#include "hw/ssi/esp32c61_spi.h"
#include "hw/dma/esp32c61_gdma.h"

#define ESP32C61_RESET_ADDRESS       0x40000000
#define ESP32C61_RESET_GPIO_NAME     "esp32c61.machine.reset_gpio"
#define MB (1024*1024)

#define ESP32C61_IRAM_SIZE           (320 * 1024)
#define ESP32C61_IROM_SIZE           (256 * 1024)

struct Esp32C61MachineState {
    MachineState parent;

    EspRISCVCPU soc;
    BusState periph_bus;

    qemu_irq cpu_reset;

    DeviceState *eth;
    ESP32C61IntMatrixState intmatrix;
    ESP32C61IntpriState intpri;
    ESP32C61UARTState uart[ESP32C61_UART_COUNT];
    ESP32C61GPIOState gpio;
    ESP32C61CacheState cache;
    ESP32C61EfuseState efuse;
    ESP32C61ClockState clock;
    ESP32C61TimgState timg[2];
    ESP32C61SysTimerState systimer;
    ESP32C61SpiState spi1;
    ESP32C61GdmaState gdma;
    ESP32C61ShaState sha;
    ESPEccState ecc;
    ESPEcdsaState ecdsa;
    ESP32C61UsbJtagState jtag;
    ESP32C61PcrState pcr;
    ESP32C61LpState lp;
    ESP32C61SpiMemState spi_mem;
    ESP32C61I2cAnaMstState i2c_ana_mst;
    ESP32C61ModemState modem;
};

#define TYPE_ESP32C61_MACHINE MACHINE_TYPE_NAME("esp32c61")
OBJECT_DECLARE_SIMPLE_TYPE(Esp32C61MachineState, ESP32C61_MACHINE)

enum MemoryRegions {
    ESP32C61_MEMREGION_IROM,
    ESP32C61_MEMREGION_DRAM,
    ESP32C61_MEMREGION_IRAM,
    ESP32C61_MEMREGION_DCACHE,
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32c61_memmap[] = {
    [ESP32C61_MEMREGION_IROM]    = { 0x40000000,  ESP32C61_IROM_SIZE },
    [ESP32C61_MEMREGION_DRAM]    = { 0x40800000,  ESP32C61_IRAM_SIZE },
    [ESP32C61_MEMREGION_IRAM]    = { 0x40800000,  ESP32C61_IRAM_SIZE },
    [ESP32C61_MEMREGION_DCACHE]  = { 0x42000000,  0x2000000 },
};

static void esp32c61_reset_request(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void esp32c61_load_rom_data_init(const char *rom_path)
{
    GMappedFile *mapped = g_mapped_file_new(rom_path, false, NULL);
    if (!mapped) {
        return;
    }
    const uint8_t *buf = (const uint8_t *)g_mapped_file_get_contents(mapped);
    gsize file_len = g_mapped_file_get_length(mapped);
    if (file_len < sizeof(Elf32_Ehdr)) {
        goto out;
    }

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)buf;
    uint32_t shoff = le32_to_cpu(ehdr->e_shoff);
    uint16_t shentsize = le16_to_cpu(ehdr->e_shentsize);
    uint16_t shnum = le16_to_cpu(ehdr->e_shnum);
    uint16_t phentsize = le16_to_cpu(ehdr->e_phentsize);
    uint16_t phnum = le16_to_cpu(ehdr->e_phnum);
    uint32_t phoff = le32_to_cpu(ehdr->e_phoff);

    uint32_t text_file_off = 0, text_vaddr = 0, text_filesz = 0;
    for (int i = 0; i < phnum; i++) {
        const Elf32_Phdr *ph =
            (const Elf32_Phdr *)(buf + phoff + i * phentsize);
        if (le32_to_cpu(ph->p_type) == PT_LOAD &&
            le32_to_cpu(ph->p_vaddr) == 0x40000000 &&
            le32_to_cpu(ph->p_filesz) > 0) {
            text_file_off = le32_to_cpu(ph->p_offset);
            text_vaddr = le32_to_cpu(ph->p_vaddr);
            text_filesz = le32_to_cpu(ph->p_filesz);
            break;
        }
    }
    if (!text_filesz) {
        goto out;
    }

    uint32_t dt_start = 0, dt_end = 0;
    for (int i = 0; i < shnum; i++) {
        const Elf32_Shdr *sh =
            (const Elf32_Shdr *)(buf + shoff + i * shentsize);
        uint32_t sh_type = le32_to_cpu(sh->sh_type);
        if (sh_type != SHT_SYMTAB) {
            continue;
        }
        uint32_t sym_off = le32_to_cpu(sh->sh_offset);
        uint32_t sym_sz = le32_to_cpu(sh->sh_size);
        uint32_t entsize = le32_to_cpu(sh->sh_entsize);
        uint32_t strtab_idx = le32_to_cpu(sh->sh_link);
        if (!entsize || strtab_idx >= shnum) {
            continue;
        }
        const Elf32_Shdr *str_sh =
            (const Elf32_Shdr *)(buf + shoff + strtab_idx * shentsize);
        uint32_t str_off = le32_to_cpu(str_sh->sh_offset);
        uint32_t str_sz = le32_to_cpu(str_sh->sh_size);

        for (uint32_t j = 0; j < sym_sz; j += entsize) {
            const Elf32_Sym *sym =
                (const Elf32_Sym *)(buf + sym_off + j);
            uint32_t name_idx = le32_to_cpu(sym->st_name);
            if (name_idx >= str_sz) {
                continue;
            }
            const char *name = (const char *)(buf + str_off + name_idx);
            uint32_t val = le32_to_cpu(sym->st_value);
            if (strcmp(name, "_data_table_start") == 0) {
                dt_start = val;
            } else if (strcmp(name, "_bss_start") == 0) {
                dt_end = val;
            }
        }
        break;
    }
    if (!dt_start || !dt_end || dt_end <= dt_start) {
        goto out;
    }

    uint32_t dt_foff = text_file_off + (dt_start - text_vaddr);
    uint32_t dt_size = dt_end - dt_start;
    if (dt_foff + dt_size > file_len) {
        goto out;
    }

    hwaddr irom_min = UINT32_MAX, irom_max = 0;
    for (uint32_t off = 0; off + 12 <= dt_size; off += 12) {
        uint32_t dest = ldl_le_p(buf + dt_foff + off);
        uint32_t dest_end = ldl_le_p(buf + dt_foff + off + 4);
        uint32_t src = ldl_le_p(buf + dt_foff + off + 8);
        uint32_t sz = dest_end - dest;
        if (sz > 0) {
            if (src < irom_min) {
                irom_min = src;
            }
            if (src + sz > irom_max) {
                irom_max = src + sz;
            }
        }
    }
    if (irom_min >= irom_max) {
        goto out;
    }

    uint32_t blob_size = irom_max - irom_min;
    uint8_t *blob = g_malloc0(blob_size);

    GHashTable *sec_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (int i = 0; i < shnum; i++) {
        const Elf32_Shdr *sh =
            (const Elf32_Shdr *)(buf + shoff + i * shentsize);
        if (le32_to_cpu(sh->sh_type) != SHT_PROGBITS) {
            continue;
        }
        uint32_t addr = le32_to_cpu(sh->sh_addr);
        uint32_t sz = le32_to_cpu(sh->sh_size);
        if (sz == 0 || addr < 0x40800000 || addr >= 0x40860000) {
            continue;
        }
        g_hash_table_insert(sec_map, GUINT_TO_POINTER(addr),
                            GUINT_TO_POINTER(i));
    }

    int filled = 0;
    for (uint32_t off = 0; off + 12 <= dt_size; off += 12) {
        uint32_t dest = ldl_le_p(buf + dt_foff + off);
        uint32_t dest_end = ldl_le_p(buf + dt_foff + off + 4);
        uint32_t src = ldl_le_p(buf + dt_foff + off + 8);
        uint32_t sz = dest_end - dest;
        if (sz == 0) {
            continue;
        }
        gpointer val;
        if (!g_hash_table_lookup_extended(sec_map, GUINT_TO_POINTER(dest),
                                          NULL, &val)) {
            continue;
        }
        int si = GPOINTER_TO_UINT(val);
        const Elf32_Shdr *sh =
            (const Elf32_Shdr *)(buf + shoff + si * shentsize);
        uint32_t sec_off = le32_to_cpu(sh->sh_offset);
        uint32_t sec_sz = le32_to_cpu(sh->sh_size);
        uint32_t copy_sz = MIN(sz, sec_sz);
        if (sec_off + copy_sz > file_len) {
            continue;
        }
        memcpy(blob + (src - irom_min), buf + sec_off, copy_sz);
        filled++;
    }

    g_hash_table_destroy(sec_map);

    if (filled > 0) {
        rom_add_blob_fixed("esp32c61.rom_data_init", blob, blob_size, irom_min);
    }
    g_free(blob);

out:
    g_mapped_file_unref(mapped);
}


static void esp32c61_init_spi_flash(Esp32C61MachineState *ms, BlockBackend *blk)
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

static void esp32c61_init_openeth(Esp32C61MachineState *ms)
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
                       qdev_get_gpio_in(DEVICE(&ms->intmatrix), 0));
}

static void esp32c61_write_boot_stub(hwaddr entry, hwaddr reset_addr)
{
    const uint32_t iram_top = 0x40800000 + ESP32C61_IRAM_SIZE;
    const uint32_t entry32 = (uint32_t)entry;

    uint32_t entry_lo = entry32 & 0xFFF;
    uint32_t entry_hi = entry32;
    if (entry_lo & 0x800) {
        entry_hi += 0x1000;
    }

    uint32_t stub[] = {
        (iram_top & 0xFFFFF000) | (2 << 7) | 0x37,
        (0x60000000 & 0xFFFFF000) | (6 << 7) | 0x37,
        (0x51 << 20) | (0 << 15) | (0 << 12) | (7 << 7) | 0x13,
        (0 << 25) | (7 << 20) | (6 << 15) | (2 << 12) | (0 << 7) | 0x23,
        (entry_hi & 0xFFFFF000) | (5 << 7) | 0x37,
        ((entry_lo & 0xFFF) << 20) | (5 << 15) | (0 << 12) | (5 << 7) | 0x13,
        (0x305 << 20) | (5 << 15) | (1 << 12) | (0 << 7) | 0x73,
        (5 << 15) | (0 << 12) | (0 << 7) | 0x67,
    };

    rom_add_blob_fixed("esp32c61.boot_stub", stub, sizeof(stub), reset_addr);
}

static void esp32c61_load_firmware(MachineState *machine)
{
    bool rom_loaded = false;

    if (machine->firmware) {
        uint64_t rom_entry = ESP32C61_RESET_ADDRESS;
        int r = load_elf_ram_sym(machine->firmware, NULL, NULL, NULL,
                                 &rom_entry, NULL, NULL, NULL, 0,
                                 EM_RISCV, 1, 0, NULL, false, NULL);
        if (r > 0) {
            qemu_log("Loaded ROM ELF, entry 0x%08" PRIx64 "\n", rom_entry);
            esp32c61_load_rom_data_init(machine->firmware);

            /*
             * ROM ELF stubs for ESP32-C61:
             *   0x40001548  ets_get_bootloader_offset  -> returns 0x20000
             *   0x4000154c  ets_set_bootloader_offset  -> no-op (ret)
             */
            static const uint32_t rom_stubs[] = {
                0x00020537,  /* lui  a0, 0x20      (a0 = 0x20000) */
                0x00008067,  /* jalr zero, ra, 0   (ret)          */
            };
            rom_add_blob_fixed("esp32c61.rom_stubs",
                               rom_stubs, sizeof(rom_stubs), 0x40001548);

            /*
             * ets_rom_layout_p (0x4003fffc) — patch to point at the
             * ROM's ets_rom_layout structure at 0x4003c934.
             */
            static const uint32_t rom_layout_ptr = 0x4003c934;
            rom_add_blob_fixed("esp32c61.rom_layout_ptr",
                               &rom_layout_ptr, sizeof(rom_layout_ptr),
                               0x4003fffc);

            rom_loaded = true;
        } else {
            int size = load_image_targphys_as(machine->firmware, ESP32C61_RESET_ADDRESS,
                                             ESP32C61_IROM_SIZE, &address_space_memory);
            if (size >= 0) {
                rom_loaded = true;
            } else {
                error_report("Error: could not load -bios '%s'", machine->firmware);
                exit(1);
            }
        }
    }

    if (!rom_loaded) {
        char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32c61-rom.bin");
        if (rom_binary == NULL) {
            error_report("Error: -bios argument not set, and esp32c61-rom.bin not found");
            exit(1);
        }

        if (rom_add_file_fixed_as(rom_binary, ESP32C61_RESET_ADDRESS, -1,
                                  &address_space_memory) < 0) {
            error_report("Error: could not load ROM binary '%s'", rom_binary);
            g_free(rom_binary);
            exit(1);
        }
        rom_loaded = true;
        g_free(rom_binary);
    }

    if (machine->kernel_filename) {
        uint64_t elf_entry = 0;
        int r = load_elf_ram_sym(machine->kernel_filename, NULL, NULL, NULL,
                                 &elf_entry, NULL, NULL, NULL, 0,
                                 EM_RISCV, 1, 0, NULL, false, NULL);
        if (r > 0) {
            qemu_log("Loaded kernel ELF at 0x%08" PRIx64 "\n", elf_entry);
            if (!rom_loaded) {
                esp32c61_write_boot_stub(elf_entry, ESP32C61_RESET_ADDRESS);
            }
        } else {
            error_report("Error: could not load kernel '%s'", machine->kernel_filename);
            exit(1);
        }
    } else if (!rom_loaded) {
        error_report("Error: no ROM, -bios, or -kernel specified");
        exit(1);
    }
}

static void esp32c61_cpu_freq_changed(void *opaque, uint32_t freq_hz)
{
    esp_cpu_set_freq(opaque, freq_hz);
}

static void esp32c61_machine_init(MachineState *machine)
{
    BlockBackend *blk = NULL;
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qemu_log("Adding SPI flash device\n");
        blk = blk_by_legacy_dinfo(dinfo);
    } else {
        qemu_log("Not initializing SPI Flash\n");
    }

    Esp32C61MachineState *ms = ESP32C61_MACHINE(machine);

    object_initialize_child(OBJECT(ms), "soc", &ms->soc, TYPE_ESP_RISCV_CPU);
    qdev_prop_set_uint64(DEVICE(&ms->soc), "resetvec", ESP32C61_RESET_ADDRESS);
    qdev_prop_set_bit(DEVICE(&ms->soc), "has-pma", true);
    qdev_prop_set_bit(DEVICE(&ms->soc), "clic-ext-irq", true);

    {
        CPURISCVState *env = &ms->soc.parent_obj.env;
        env->misa_ext |= RVA;
        env->misa_ext_mask |= RVA;
        Object *soc_obj = OBJECT(&ms->soc);
        object_property_set_bool(soc_obj, "zca", true, &error_abort);
        object_property_set_bool(soc_obj, "zcb", true, &error_abort);
        object_property_set_bool(soc_obj, "zcmp", true, &error_abort);
        object_property_set_bool(soc_obj, "zcmt", true, &error_abort);
        object_property_set_bool(soc_obj, "zcd", false, &error_abort);
        object_property_set_bool(soc_obj, "zcf", false, &error_abort);
        object_property_set_bool(soc_obj, "zicsr", true, &error_abort);
    }

    const struct MemmapEntry *memmap = esp32c61_memmap;
    MemoryRegion *sys_mem = get_system_memory();

    MemoryRegion *romdata = g_new(MemoryRegion, 1);
    memory_region_init_ram(romdata, NULL, "esp32c61.romdata", 0x20000, &error_fatal);
    memory_region_add_subregion(sys_mem, 0x00000000, romdata);

    MemoryRegion *irom = g_new(MemoryRegion, 1);
    memory_region_init_ram(irom, NULL, "esp32c61.irom",
                          memmap[ESP32C61_MEMREGION_IROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C61_MEMREGION_IROM].base, irom);

    MemoryRegion *iram = g_new(MemoryRegion, 1);
    memory_region_init_ram(iram, NULL, "esp32c61.iram",
                          memmap[ESP32C61_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32C61_MEMREGION_IRAM].base, iram);

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    memory_region_init_alias(dram, NULL, "esp32c61.dram", iram, 0,
                             memmap[ESP32C61_MEMREGION_DRAM].size);
    memory_region_add_subregion(sys_mem, memmap[ESP32C61_MEMREGION_DRAM].base, dram);

    object_initialize_child(OBJECT(machine), "extmem", &ms->cache, TYPE_ESP32C61_CACHE);
    if (blk) {
        ms->cache.parent.flash_blk = blk;
    }
    sysbus_realize(SYS_BUS_DEVICE(&ms->cache), &error_fatal);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EXTMEM_BASE,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->cache), 0), 0);
    memory_region_add_subregion_overlap(sys_mem, ms->cache.parent.dcache_base,
                                        &ms->cache.parent.dcache, 0);

    esp32c61_load_firmware(machine);

    qdev_realize(DEVICE(&ms->soc), NULL, &error_fatal);

    qbus_init(&ms->periph_bus, sizeof(ms->periph_bus),
              TYPE_SYSTEM_BUS, DEVICE(&ms->soc), "esp32c61-periph-bus");

    qdev_init_gpio_in_named(DEVICE(&ms->soc), esp32c61_reset_request,
                           ESP32C61_RESET_GPIO_NAME, 1);

    for (int i = 0; i < ESP32C61_UART_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "uart%d", i);
        object_initialize_child(OBJECT(machine), name, &ms->uart[i], TYPE_ESP32C61_UART);
        snprintf(name, sizeof(name), "serial%d", i);
        object_property_add_alias(OBJECT(machine), name, OBJECT(&ms->uart[i]), "chardev");
        qdev_prop_set_chr(DEVICE(&ms->uart[i]), "chardev", serial_hd(i));
    }

    object_initialize_child(OBJECT(machine), "intmatrix", &ms->intmatrix, TYPE_ESP32C61_INTMATRIX);
    object_initialize_child(OBJECT(machine), "intpri", &ms->intpri, TYPE_ESP32C61_INTPRI);
    object_initialize_child(OBJECT(machine), "gpio", &ms->gpio, TYPE_ESP32C61_GPIO);
    object_initialize_child(OBJECT(machine), "efuse", &ms->efuse, TYPE_ESP32C61_EFUSE);
    object_initialize_child(OBJECT(machine), "clock", &ms->clock, TYPE_ESP32C61_CLOCK);
    object_initialize_child(OBJECT(machine), "gdma", &ms->gdma, TYPE_ESP32C61_GDMA);
    object_initialize_child(OBJECT(machine), "sha", &ms->sha, TYPE_ESP32C61_SHA);
    object_initialize_child(OBJECT(machine), "ecc", &ms->ecc, TYPE_ESP32C61_ECC);
    object_initialize_child(OBJECT(machine), "ecdsa", &ms->ecdsa, TYPE_ESP32C61_ECDSA);
    object_initialize_child(OBJECT(machine), "timg0", &ms->timg[0], TYPE_ESP32C61_TIMG);
    object_initialize_child(OBJECT(machine), "timg1", &ms->timg[1], TYPE_ESP32C61_TIMG);
    for (int i = 0; i < 2; i++) {
        ESPTimgState *timg = ESP_TIMG(&ms->timg[i]);
        timg->xtal_freq = 40000000;
        /*
         * The guest selects the clock to calibrate in PCR, which the TIMG model
         * does not read, and its CLK_SEL field reads as 1. Calibrate at the LP
         * timer rate.
         */
        timg->cali_osc_freq[1] = 150000;
        timg->cali_osc_freq[2] = 32000;
        timg->cali_osc_freq[3] = 136000;
        timg->cali_osc_freq[4] = ESP_RC_FAST_FREQ / 256;
    }
    object_initialize_child(OBJECT(machine), "systimer", &ms->systimer, TYPE_ESP32C61_SYSTIMER);
    object_initialize_child(OBJECT(machine), "spi1", &ms->spi1, TYPE_ESP32C61_SPI);
    object_initialize_child(OBJECT(machine), "jtag", &ms->jtag, TYPE_ESP32C61_JTAG);
    object_initialize_child(OBJECT(machine), "pcr", &ms->pcr, TYPE_ESP32C61_PCR);
    object_initialize_child(OBJECT(machine), "lp", &ms->lp, TYPE_ESP32C61_LP);
    object_initialize_child(OBJECT(machine), "spi_mem", &ms->spi_mem, TYPE_ESP32C61_SPI_MEM);
    object_initialize_child(OBJECT(machine), "i2c_ana_mst", &ms->i2c_ana_mst, TYPE_ESP32C61_I2C_ANA_MST);
    object_initialize_child(OBJECT(machine), "modem_lpcon", &ms->modem, TYPE_ESP32C61_MODEM);

    /* Interrupt matrix + CLIC */
    DeviceState *intmatrix_dev = DEVICE(&ms->intmatrix);
    {
        object_property_set_link(OBJECT(&ms->intmatrix), "cpu", OBJECT(&ms->soc), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->intmatrix), &error_fatal);

        MemoryRegion *map_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTERRUPT_BASE, map_mr, 0);

        MemoryRegion *clic_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix), 1);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_CLIC_BASE, clic_mr, 0);

        for (int i = 0; i <= ESP32C61_CPU_INT_COUNT; i++) {
            qemu_irq cpu_input = qdev_get_gpio_in_named(DEVICE(&ms->soc), ESP_CPU_IRQ_LINES_NAME, i);
            qdev_connect_gpio_out_named(intmatrix_dev, ESP32C61_INT_MATRIX_OUTPUT_NAME, i, cpu_input);
        }
    }

    esp32c61_init_openeth(ms);

    /* USB Serial JTAG */
    {
        qdev_prop_set_chr(DEVICE(&ms->jtag), "chardev", serial_hd(ESP32C61_UART_COUNT));
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
            esp32c61_init_spi_flash(ms, blk);
        }
    }

    /* UARTs — 3 UARTs for C61 */
    {
        const hwaddr uart_base[] = { DR_REG_UART_BASE, DR_REG_UART1_BASE, DR_REG_UART2_BASE };
        for (int i = 0; i < ESP32C61_UART_COUNT; ++i) {
            sysbus_realize(SYS_BUS_DEVICE(&ms->uart[i]), &error_fatal);
            MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->uart[i]), 0);
            memory_region_add_subregion_overlap(sys_mem, uart_base[i], mr, 0);
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->uart[i]), 0,
                               qdev_get_gpio_in(intmatrix_dev, C61_ETS_UART0_INTR_SOURCE + i));
        }
    }

    /* GPIO + IO_MUX */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->gpio), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gpio), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, mr, 0);
        MemoryRegion *iomux = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gpio), 1);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_IO_MUX_BASE, iomux, 0);
    }

    /* eFuse — priority 1 to win over LP catch-all */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->efuse), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->efuse), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EFUSE_BASE, mr, 1);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->efuse), 0,
                           qdev_get_gpio_in(intmatrix_dev, C61_ETS_EFUSE_INTR_SOURCE));
    }

    /* PCR */
    {
        ms->pcr.cpu_freq_changed = esp32c61_cpu_freq_changed;
        ms->pcr.cpu_freq_opaque = &ms->soc;
        sysbus_realize(SYS_BUS_DEVICE(&ms->pcr), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->pcr), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSCON_BASE, mr, 0);
    }

    /* LP domain */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->lp), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->lp), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C61_LP_BASE, mr, 0);
        qdev_connect_gpio_out_named(DEVICE(&ms->lp), ESP32C61_LP_RESET_GPIO, 0,
                                    qdev_get_gpio_in_named(DEVICE(&ms->soc),
                                        ESP32C61_RESET_GPIO_NAME, 0));
    }

    /* SPI0 MEM controller */
    {
        if (blk) {
            ms->spi_mem.parent.flash_blk = blk;
        }
        ms->spi_mem.parent.dcache_mr = &ms->cache.parent.dcache;
        sysbus_realize(SYS_BUS_DEVICE(&ms->spi_mem), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi_mem), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C61_SPI_MEM_BASE, mr, 0);
    }

    /* I2C Analog Master */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->i2c_ana_mst), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->i2c_ana_mst), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C61_I2C_ANA_MST_BASE, mr, 0);
    }

    /* GDMA */
    {
        object_property_set_link(OBJECT(&ms->gdma), "soc_mr", OBJECT(dram), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->gdma), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gdma), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GDMA_BASE, mr, 0);
        for (int i = 0; i < ESP32C61_GDMA_CHANNEL_COUNT; i++) {
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_IN_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, C61_ETS_DMA_IN_CH0_INTR_SOURCE + i));
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_OUT_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, C61_ETS_DMA_OUT_CH0_INTR_SOURCE + i));
        }
    }

    /* SHA */
    {
        ms->sha.parent.parent.gdma = ESP_GDMA(&ms->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ms->sha), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->sha), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SHA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->sha), 0,
                           qdev_get_gpio_in(intmatrix_dev, C61_ETS_SHA_INTR_SOURCE));
    }

    /* ECC */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->ecc), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->ecc), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_ECC_MULT_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->ecc), 0,
                           qdev_get_gpio_in(intmatrix_dev, C61_ETS_ECC_INTR_SOURCE));
    }

    /* ECDSA */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->ecdsa), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->ecdsa), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_ECDSA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->ecdsa), 0,
                           qdev_get_gpio_in(intmatrix_dev, C61_ETS_ECDSA_INTR_SOURCE));
    }

    /* MODEM_LPCON */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->modem), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->modem), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32C61_MODEM_BASE, mr, 0);
    }

    /* System clock */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->clock), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->clock), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTEM_BASE, mr, 0);
    }

    /* INTPRI */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->intpri), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intpri), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTPRI_BASE, mr, 0);

        for (int i = 0; i < ESP32C61_INTPRI_CPU_INTR_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->intpri), i,
                               qdev_get_gpio_in(intmatrix_dev, C61_ETS_FROM_CPU_INTR0_SOURCE + i));
        }
    }

    /* Timer Groups */
    for (int tg = 0; tg < 2; tg++) {
        sysbus_realize(SYS_BUS_DEVICE(&ms->timg[tg]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->timg[tg]), 0);
        const hwaddr timg_base[] = { DR_REG_TIMERGROUP0_BASE, DR_REG_TIMERGROUP1_BASE };
        memory_region_add_subregion_overlap(sys_mem, timg_base[tg], mr, 0);

        const int tg0_src[] = { C61_ETS_TG0_T0_LEVEL_INTR_SOURCE, C61_ETS_TG0_WDT_LEVEL_INTR_SOURCE };
        const int tg1_src[] = { C61_ETS_TG1_T0_LEVEL_INTR_SOURCE, C61_ETS_TG1_WDT_LEVEL_INTR_SOURCE };
        const int *src = tg == 0 ? tg0_src : tg1_src;

        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32C61_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, src[0]));
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32C61_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, src[1]));

        const int wdt_reset_reason = tg == 0 ? ESP32C61_CORE_MWDT0 : ESP32C61_CORE_MWDT1;
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32C61_WDT_IRQ_RESET, 0,
                                    qdev_get_gpio_in(DEVICE(&ms->lp), wdt_reset_reason));
    }

    /* System timer */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->systimer), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->systimer), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTIMER_BASE, mr, 0);
        for (int i = 0; i < ESP_SYSTIMER_IRQ_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->systimer), i,
                               qdev_get_gpio_in(intmatrix_dev, C61_ETS_SYSTIMER_TARGET0_INTR_SOURCE + i));
        }
    }

    create_unimplemented_device("esp32c61.peripherals", ESP32C61_IO_START_ADDR, 0x100000);
    create_unimplemented_device("esp32c61.clic_area", 0x20000000, 0x1000000);
}

static void esp32c61_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Espressif ESP32-C61 machine";
    mc->default_cpu_type = TYPE_ESP_RISCV_CPU;
    mc->init = esp32c61_machine_init;
    mc->max_cpus = 1;
    mc->default_cpus = 1;
    mc->default_ram_size = 320 * 1024;
}

static const TypeInfo esp32c61_info = {
    .name = TYPE_ESP32C61_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Esp32C61MachineState),
    .class_init = esp32c61_machine_class_init,
};

static void esp32c61_machine_type_init(void)
{
    type_register_static(&esp32c61_info);
}

type_init(esp32c61_machine_type_init);

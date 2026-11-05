/*
 * ESP32-H4 SoC and machine
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
#include "hw/irq.h"
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
#include "sysemu/cpu-timers.h"
#include "qemu/seqlock.h"
#include "sysemu/cpu-timers-internal.h"
#include "qemu/timer.h"
#include "net/net.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/misc/esp32h4_reg.h"
#include "hw/misc/esp32h4_cache.h"
#include "hw/misc/esp32h4_pcr.h"
#include "hw/misc/esp32h4_lp.h"
#include "hw/misc/esp32h4_spi_mem.h"
#include "hw/misc/esp32h4_i2c_ana_mst.h"
#include "hw/misc/esp32h4_jtag.h"
#include "hw/misc/esp32h4_modem.h"
#include "hw/misc/unimp.h"
#include "hw/char/esp32h4_uart.h"
#include "hw/gpio/esp32h4_gpio.h"
#include "hw/nvram/esp32h4_efuse.h"
#include "hw/riscv/esp32h4_clk.h"
#include "hw/riscv/esp32h4_intmatrix.h"
#include "hw/misc/esp32h4_intpri.h"
#include "hw/misc/esp32h4_sha.h"
#include "hw/timer/esp32h4_timg.h"
#include "hw/timer/esp32h4_systimer.h"
#include "hw/ssi/esp32h4_spi.h"
#include "hw/dma/esp32h4_gdma.h"

#define ESP32H4_RESET_ADDRESS       0x40000000
#define ESP32H4_CORE1_RESET_HOLD_NS (100 * SCALE_US)
#define ESP32H4_RESET_GPIO_NAME     "esp32h4.machine.reset_gpio"
#define MB (1024*1024)

#define ESP32H4_IRAM_SIZE           (320 * 1024)
#define ESP32H4_RTC_SIZE            (16 * 1024)
#define ESP32H4_IROM_SIZE           (128 * 1024)

/*
 * QEMU-only MMIO used by the patched ets_delay_us ROM stub.
 * Writing the delay (microseconds) warps QEMU_CLOCK_VIRTUAL forward so
 * busy-wait calibration delays do not burn host wall time under -icount.
 */
#define ESP32H4_QEMU_DELAY_US_ADDR  0x3F000000

struct Esp32H4MachineState {
    MachineState parent;

    unsigned num_cpus;
    EspRISCVCPU cpu[ESP32H4_CPU_COUNT];
    /*
     * The CLIC window is aliased per hart: each core reaches its own CLIC at
     * DR_REG_CLIC_BASE and the other core's at DR_REG_CLIC_CORE1_BASE,
     * whichever core it is running on. That cannot be expressed in the shared
     * system memory map, so every hart gets its own address space: a container
     * holding an alias of system memory plus the two CLIC views.
     */
    MemoryRegion cpu_mem[ESP32H4_CPU_COUNT];
    BusState periph_bus;

    /* core1 runs only once it is clocked, out of reset and not stalled */
    bool core1_clk_run;
    bool core1_stalled;
    bool core1_reset_pending;
    QEMUTimer core1_reset_hold;
    /* core1 was halted in WFI when it was stalled */
    bool core1_idle;

    qemu_irq cpu_reset;
    MemoryRegion qemu_delay_mmio;
    /* HP_SYSTEM_CORE_DEBUG_RUNSTALL_CONF, over the clock window at +0x40. */
    MemoryRegion runstall_mmio;

    DeviceState *eth;
    ESP32H4IntMatrixState intmatrix[ESP32H4_CPU_COUNT];
    ESP32H4IntpriState intpri;
    ESP32H4UARTState uart[ESP32H4_UART_COUNT];
    ESP32H4GPIOState gpio;
    ESP32H4CacheState cache;
    ESP32H4EfuseState efuse;
    ESP32H4ClockState clock;
    ESP32H4TimgState timg[2];
    ESP32H4SysTimerState systimer;
    ESP32H4SpiState spi1;
    ESP32H4GdmaState gdma;
    ESP32H4ShaState sha;
    ESP32H4UsbJtagState jtag;
    ESP32H4PcrState pcr;
    ESP32H4LpState lp;
    ESP32H4SpiMemState spi_mem;
    ESP32H4I2cAnaMstState i2c_ana_mst;
    ESP32H4ModemState modem;
};

#define TYPE_ESP32H4_MACHINE MACHINE_TYPE_NAME("esp32h4")
OBJECT_DECLARE_SIMPLE_TYPE(Esp32H4MachineState, ESP32H4_MACHINE)

enum MemoryRegions {
    ESP32H4_MEMREGION_IROM,
    ESP32H4_MEMREGION_DRAM,
    ESP32H4_MEMREGION_IRAM,
    ESP32H4_MEMREGION_RTCFAST,
    ESP32H4_MEMREGION_DCACHE,
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32h4_memmap[] = {
    [ESP32H4_MEMREGION_IROM]    = { 0x40000000,  ESP32H4_IROM_SIZE },
    [ESP32H4_MEMREGION_DRAM]    = { 0x40810000,  ESP32H4_IRAM_SIZE },
    [ESP32H4_MEMREGION_IRAM]    = { 0x40810000,  ESP32H4_IRAM_SIZE },
    [ESP32H4_MEMREGION_RTCFAST] = { 0x50000000,  ESP32H4_RTC_SIZE },
    [ESP32H4_MEMREGION_DCACHE]  = { 0x42000000,  0x2000000 },
};

static void esp32h4_reset_request(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

/* Apply the requested core1 state. Runs on core1's thread: core1 may be executing. */
static void esp32h4_core1_apply(CPUState *cs, run_on_cpu_data data)
{
    Esp32H4MachineState *ms = data.host_ptr;
    EspRISCVCPU *cpu = ESP_CPU(cs);
    const bool run = ms->core1_clk_run && !ms->core1_stalled &&
                     !timer_pending(&ms->core1_reset_hold);

    /* A stall keeps the core's state, including whether it was waiting in WFI */
    if (!cpu->stalled) {
        ms->core1_idle = cs->halted;
    }

    if (ms->core1_reset_pending) {
        ms->core1_reset_pending = false;
        ms->core1_idle = false;
        cpu_reset(cs);
    }

    cpu->stalled = !run;
    cs->halted = run ? ms->core1_idle : 1;
}

/*
 * Core1 boots the same mask ROM as core0. The ROM dispatches on mhartid and parks
 * core1 in a loop polling HP_SYSTEM_APPCPU_BOOT_ADDR until core0 sets an entry point.
 */
static void esp32h4_core1_refresh(Esp32H4MachineState *ms)
{
    if (ms->num_cpus < 2) {
        return;
    }

    async_run_on_cpu(CPU(&ms->cpu[1]), esp32h4_core1_apply,
                     RUN_ON_CPU_HOST_PTR(ms));
}

static void esp32h4_core1_reset_hold_done(void *opaque)
{
    esp32h4_core1_refresh(opaque);
}

static void esp32h4_core1_run_request(void *opaque, int n, int level)
{
    Esp32H4MachineState *ms = ESP32H4_MACHINE(opaque);
    ms->core1_clk_run = level;
    if (!level) {
        ms->core1_reset_pending = true;
    }
    esp32h4_core1_refresh(ms);
}

static void esp32h4_core1_stall_request(void *opaque, int n, int level)
{
    Esp32H4MachineState *ms = ESP32H4_MACHINE(opaque);
    ms->core1_stalled = level;
    esp32h4_core1_refresh(ms);
}

static void esp32h4_core1_sw_reset_request(void *opaque, int n, int level)
{
    Esp32H4MachineState *ms = ESP32H4_MACHINE(opaque);

    if (level) {
        /*
         * IDF writes the stall code right after the reset bit and relies on core1
         * not reaching the boot address check in between.
         */
        timer_mod(&ms->core1_reset_hold, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                         ESP32H4_CORE1_RESET_HOLD_NS);
        ms->core1_reset_pending = true;
        esp32h4_core1_refresh(ms);
    }
}

static void esp32h4_cpu_freq_changed(void *opaque, uint32_t freq_hz)
{
    Esp32H4MachineState *ms = ESP32H4_MACHINE(opaque);

    for (unsigned i = 0; i < ms->num_cpus; i++) {
        esp_cpu_set_freq(&ms->cpu[i], freq_hz);
    }
}

/*
 * A peripheral interrupt line is shared by both cores: each core's matrix maps
 * it independently, so the source has to be delivered to every matrix.
 */
typedef struct Esp32H4IrqFanout {
    qemu_irq out[ESP32H4_CPU_COUNT];
    unsigned count;
} Esp32H4IrqFanout;

static void esp32h4_irq_fanout(void *opaque, int n, int level)
{
    Esp32H4IrqFanout *f = opaque;

    for (unsigned i = 0; i < f->count; i++) {
        qemu_set_irq(f->out[i], level);
    }
}

static qemu_irq esp32h4_intmatrix_in(Esp32H4MachineState *ms, int source)
{
    if (ms->num_cpus < 2) {
        return qdev_get_gpio_in(DEVICE(&ms->intmatrix[0]), source);
    }

    Esp32H4IrqFanout *f = g_new0(Esp32H4IrqFanout, 1);
    f->count = ms->num_cpus;
    for (unsigned i = 0; i < ms->num_cpus; i++) {
        f->out[i] = qdev_get_gpio_in(DEVICE(&ms->intmatrix[i]), source);
    }
    return qemu_allocate_irq(esp32h4_irq_fanout, f, 0);
}

/* HP_SYSTEM_CORE_DEBUG_RUNSTALL_CONF: bit 2 is CORE1_RUNSTALLED. */
static uint64_t esp32h4_runstall_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp32H4MachineState *ms = ESP32H4_MACHINE(opaque);

    return ms->core1_stalled ? (1U << 2) : 0;
}

static void esp32h4_runstall_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
}

static const MemoryRegionOps esp32h4_runstall_ops = {
    .read = esp32h4_runstall_read,
    .write = esp32h4_runstall_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * Advance QEMU_CLOCK_VIRTUAL under -icount without executing guest spins.
 * H4-local helper (does not change shared icount APIs).
 *
 * Step through timer deadlines so multi-stage devices (MWDT INT then RESET)
 * still fire in order when a single ets_delay_us() spans several stages.
 */
static void esp32h4_warp_virtual_ns(int64_t ns)
{
    if (!icount_enabled() || ns <= 0) {
        return;
    }

    int64_t remaining = ns;

    while (remaining > 0) {
        int64_t step = remaining;
        const int64_t until = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL, ~0);

        if (until != -1 && until >= 0 && until < step) {
            step = until > 0 ? until : 1;
        }

        seqlock_write_lock(&timers_state.vm_clock_seqlock,
                           &timers_state.vm_clock_lock);
        qatomic_set_i64(&timers_state.qemu_icount_bias,
                        qatomic_read_i64(&timers_state.qemu_icount_bias) + step);
        seqlock_write_unlock(&timers_state.vm_clock_seqlock,
                             &timers_state.vm_clock_lock);
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        remaining -= step;
    }
}

static uint64_t esp32h4_qemu_delay_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void esp32h4_qemu_delay_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    /* value = requested delay in microseconds (from patched ets_delay_us). */
    esp32h4_warp_virtual_ns((int64_t)value * 1000);
}

static const MemoryRegionOps esp32h4_qemu_delay_ops = {
    .read = esp32h4_qemu_delay_read,
    .write = esp32h4_qemu_delay_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void esp32h4_load_rom_data_init(const char *rom_path)
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
        rom_add_blob_fixed("esp32h4.rom_data_init", blob, blob_size, irom_min);
    }
    g_free(blob);

out:
    g_mapped_file_unref(mapped);
}


static void esp32h4_init_spi_flash(Esp32H4MachineState *ms, BlockBackend *blk)
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

static void esp32h4_init_openeth(Esp32H4MachineState *ms)
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

    sysbus_connect_irq(sbd, 0, esp32h4_intmatrix_in(ms, 0));
}

static void esp32h4_write_boot_stub(hwaddr entry, hwaddr reset_addr)
{
    const uint32_t iram_top = 0x40810000 + ESP32H4_IRAM_SIZE;
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

    rom_add_blob_fixed("esp32h4.boot_stub", stub, sizeof(stub), reset_addr);
}

static void esp32h4_load_firmware(MachineState *machine)
{
    bool rom_loaded = false;

    if (machine->firmware) {
        uint64_t rom_entry = ESP32H4_RESET_ADDRESS;
        int r = load_elf_ram_sym(machine->firmware, NULL, NULL, NULL,
                                 &rom_entry, NULL, NULL, NULL, 0,
                                 EM_RISCV, 1, 0, NULL, false, NULL);
        if (r > 0) {
            qemu_log("Loaded ROM ELF, entry 0x%08" PRIx64 "\n", rom_entry);
            esp32h4_load_rom_data_init(machine->firmware);

            /*
             * ROM ELF stubs for ESP32-H4:
             *   0x40001548  ets_get_bootloader_offset  -> returns 0x20000
             *   0x4000154c  ets_set_bootloader_offset  -> no-op (ret)
             */
            static const uint32_t rom_stubs[] = {
                0x00020537,  /* lui  a0, 0x20      (a0 = 0x20000) */
                0x00008067,  /* jalr zero, ra, 0   (ret)          */
            };
            rom_add_blob_fixed("esp32h4.rom_stubs",
                               rom_stubs, sizeof(rom_stubs), 0x40001548);

            /*
             * ets_rom_layout_p (0x4003fffc) — patch to point at the
             * ROM's ets_rom_layout structure at 0x4003c934.
             */
            static const uint32_t rom_layout_ptr = 0x4003c934;
            rom_add_blob_fixed("esp32h4.rom_layout_ptr",
                               &rom_layout_ptr, sizeof(rom_layout_ptr),
                               0x4003fffc);

            rom_loaded = true;
        } else {
            int size = load_image_targphys_as(machine->firmware, ESP32H4_RESET_ADDRESS,
                                             ESP32H4_IROM_SIZE, &address_space_memory);
            if (size >= 0) {
                rom_loaded = true;
            } else {
                error_report("Error: could not load -bios '%s'", machine->firmware);
                exit(1);
            }
        }
    }

    if (!rom_loaded) {
        char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32h4-rom.bin");
        if (rom_binary == NULL) {
            error_report("Error: -bios argument not set, and esp32h4-rom.bin not found");
            exit(1);
        }

        ssize_t size = get_image_size(rom_binary);
        if (size <= 0) {
            error_report("Error: could not load ROM binary '%s'", rom_binary);
            g_free(rom_binary);
            exit(1);
        }
        uint8_t *rom_buf = g_malloc(size);
        if (load_image_size(rom_binary, rom_buf, size) != size) {
            error_report("Error: could not read ROM binary '%s'", rom_binary);
            g_free(rom_buf);
            g_free(rom_binary);
            exit(1);
        }

        /*
         * Replace ets_delay_us body (trampoline at 0x4000003c -> 0x40002238)
         * with a QEMU-only stub:
         *   lui a5, ESP32H4_QEMU_DELAY_US_ADDR
         *   sw  a0, 0(a5)          # warp virtual clock by a0 microseconds
         *   ret
         *
         * Why H4-only / why not keep the ROM spin loop:
         * RTC calibration calls esp_rom_delay_us(~31ms) before polling TIMG
         * RDY. QEMU completes calibration instantly, but the guest still
         * always waits the estimate. Other SoCs keep ticks_per_us≈160 so that
         * busy-wait under -icount is cheap (and slightly short vs MWDT). H4
         * previously forced ticks=1000 for correct MWDT timing, which made the
         * same RTC wait cost ~9s of host wall time. Warping VIRTUAL time keeps
         * MWDT/esp_timer alignment without the spin.
         */
        {
            const uint32_t delay_stub[] = {
                /* lui a5, %hi(ESP32H4_QEMU_DELAY_US_ADDR) */
                (ESP32H4_QEMU_DELAY_US_ADDR & 0xFFFFF000) | (15 << 7) | 0x37,
                /* sw a0, 0(a5) */
                (0 << 25) | (10 << 20) | (15 << 15) | (2 << 12) | (0 << 7) | 0x23,
                /* jalr x0, 0(ra)  (ret) */
                0x00008067,
            };
            const size_t stub_off = 0x40002238 - ESP32H4_RESET_ADDRESS;
            if (stub_off + sizeof(delay_stub) <= (size_t)size) {
                memcpy(rom_buf + stub_off, delay_stub, sizeof(delay_stub));
            }
        }

        /*
         * IDF's esp32h4.rom.libgcc.ld is offset +4 from the real ROM
         * __call_* trampoline table (e.g. IDF __udivdi3=0x400008ec points at
         * ROM's __udivmoddi4). Re-encode each IDF slot to jump to the same
         * target as the correct ROM wrapper at idf_addr - 4.
         */
        {
            const uint32_t table_first = 0x400007e4; /* IDF __absvdi2 */
            const uint32_t table_last  = 0x4000090c; /* IDF __trunctfdf2 */
            for (uint32_t idf_addr = table_first; idf_addr <= table_last;
                 idf_addr += 4) {
                const uint32_t src_addr = idf_addr - 4;
                const size_t src_off = src_addr - ESP32H4_RESET_ADDRESS;
                const size_t dst_off = idf_addr - ESP32H4_RESET_ADDRESS;
                if (src_off + 4 > (size_t)size || dst_off + 4 > (size_t)size) {
                    break;
                }
                uint32_t insn = ldl_p(rom_buf + src_off);
                uint32_t imm20 = (insn >> 31) & 1;
                uint32_t imm10_1 = (insn >> 21) & 0x3ff;
                uint32_t imm11 = (insn >> 20) & 1;
                uint32_t imm19_12 = (insn >> 12) & 0xff;
                int32_t imm = (int32_t)((imm20 << 20) | (imm19_12 << 12)
                                        | (imm11 << 11) | (imm10_1 << 1));
                if (imm & 0x100000) {
                    imm |= ~0x1fffff; /* sign-extend 21-bit */
                }
                uint32_t target = src_addr + imm;
                int32_t new_imm = (int32_t)(target - idf_addr);
                uint32_t new_insn =
                    (((uint32_t)(new_imm >> 20) & 1) << 31)
                    | (((uint32_t)(new_imm >> 1) & 0x3ff) << 21)
                    | (((uint32_t)(new_imm >> 11) & 1) << 20)
                    | (((uint32_t)(new_imm >> 12) & 0xff) << 12)
                    | 0x6f;
                stl_p(rom_buf + dst_off, new_insn);
            }
        }

        rom_add_blob_fixed("esp32h4-rom.bin",
                           rom_buf, size, ESP32H4_RESET_ADDRESS);
        rom_loaded = true;
        g_free(rom_buf);
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
                esp32h4_write_boot_stub(elf_entry, ESP32H4_RESET_ADDRESS);
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

static void esp32h4_machine_init(MachineState *machine)
{
    BlockBackend *blk = NULL;
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qemu_log("Adding SPI flash device\n");
        blk = blk_by_legacy_dinfo(dinfo);
    } else {
        qemu_log("Not initializing SPI Flash\n");
    }

    Esp32H4MachineState *ms = ESP32H4_MACHINE(machine);
    const struct MemmapEntry *memmap = esp32h4_memmap;
    MemoryRegion *sys_mem = get_system_memory();

    ms->num_cpus = machine->smp.cpus;
    ms->core1_reset_pending = true;
    timer_init_ns(&ms->core1_reset_hold, QEMU_CLOCK_VIRTUAL,
                  esp32h4_core1_reset_hold_done, ms);

    for (unsigned i = 0; i < ms->num_cpus; i++) {
        char name[40];

        snprintf(name, sizeof(name), "cpu%u", i);
        object_initialize_child(OBJECT(ms), name, &ms->cpu[i],
                                TYPE_ESP_RISCV_CPU);
        DeviceState *cpu_dev = DEVICE(&ms->cpu[i]);
        qdev_prop_set_uint64(cpu_dev, "resetvec", ESP32H4_RESET_ADDRESS);
        qdev_prop_set_uint32(cpu_dev, "hartid-base", i);
        qdev_prop_set_bit(cpu_dev, "has-pma", true);
        qdev_prop_set_bit(cpu_dev, "clic-ext-irq", true);

        CPURISCVState *env = &ms->cpu[i].parent_obj.env;
        /* H4 has the FPU (SOC_CPU_HAS_FPU); ISR prologues use fsw/flw. */
        env->misa_ext |= RVA | RVF;
        env->misa_ext_mask |= RVA | RVF;
        Object *cpu_obj = OBJECT(&ms->cpu[i]);
        object_property_set_bool(cpu_obj, "zca", true, &error_abort);
        object_property_set_bool(cpu_obj, "zcb", true, &error_abort);
        object_property_set_bool(cpu_obj, "zcmp", true, &error_abort);
        object_property_set_bool(cpu_obj, "zcmt", true, &error_abort);
        object_property_set_bool(cpu_obj, "zcd", false, &error_abort);
        object_property_set_bool(cpu_obj, "zcf", false, &error_abort);
        object_property_set_bool(cpu_obj, "zicsr", true, &error_abort);

        /*
         * Per-hart address space, so that DR_REG_CLIC_BASE can resolve to a
         * different CLIC depending on which core issues the access. The CLIC
         * views themselves are attached once the matrices are realized.
         */
        CPUState *cs = CPU(&ms->cpu[i]);
        snprintf(name, sizeof(name), "esp32h4.cpu%u-mem", i);
        memory_region_init(&ms->cpu_mem[i], OBJECT(ms), name, UINT32_MAX);

        MemoryRegion *sysmem_view = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "esp32h4.cpu%u-sysmem", i);
        memory_region_init_alias(sysmem_view, OBJECT(ms), name, sys_mem, 0,
                                 UINT32_MAX);
        memory_region_add_subregion_overlap(&ms->cpu_mem[i], 0, sysmem_view, 0);

        cs->num_ases = 1;
        cpu_address_space_init(cs, 0, "cpu-memory", &ms->cpu_mem[i]);
        cs->memory = &ms->cpu_mem[i];

        /* PCR holds core1 in reset until core0 releases it */
        cs->start_powered_off = (i != 0);
        ms->cpu[i].stalled = (i != 0);
    }

    MemoryRegion *romdata = g_new(MemoryRegion, 1);
    memory_region_init_ram(romdata, NULL, "esp32h4.romdata", 0x20000, &error_fatal);
    memory_region_add_subregion(sys_mem, 0x00000000, romdata);

    MemoryRegion *irom = g_new(MemoryRegion, 1);
    memory_region_init_ram(irom, NULL, "esp32h4.irom",
                          memmap[ESP32H4_MEMREGION_IROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32H4_MEMREGION_IROM].base, irom);

    MemoryRegion *iram = g_new(MemoryRegion, 1);
    memory_region_init_ram(iram, NULL, "esp32h4.iram",
                          memmap[ESP32H4_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32H4_MEMREGION_IRAM].base, iram);

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    memory_region_init_alias(dram, NULL, "esp32h4.dram", iram, 0,
                             memmap[ESP32H4_MEMREGION_DRAM].size);
    memory_region_add_subregion(sys_mem, memmap[ESP32H4_MEMREGION_DRAM].base, dram);

    MemoryRegion *rtcram = g_new(MemoryRegion, 1);
    memory_region_init_ram(rtcram, NULL, "esp32h4.rtcram",
                          memmap[ESP32H4_MEMREGION_RTCFAST].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32H4_MEMREGION_RTCFAST].base, rtcram);

    memory_region_init_io(&ms->qemu_delay_mmio, NULL, &esp32h4_qemu_delay_ops, ms,
                          "esp32h4.qemu_delay_us", 4);
    memory_region_add_subregion(sys_mem, ESP32H4_QEMU_DELAY_US_ADDR,
                                &ms->qemu_delay_mmio);

    object_initialize_child(OBJECT(machine), "extmem", &ms->cache, TYPE_ESP32H4_CACHE);
    if (blk) {
        ms->cache.flash_blk = blk;
    }
    sysbus_realize(SYS_BUS_DEVICE(&ms->cache), &error_fatal);
    memory_region_add_subregion_overlap(sys_mem, DR_REG_EXTMEM_BASE,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->cache), 0), 0);
    memory_region_add_subregion_overlap(sys_mem, ms->cache.dcache_base,
                                        &ms->cache.dcache, 0);

    esp32h4_load_firmware(machine);

    for (unsigned i = 0; i < ms->num_cpus; i++) {
        qdev_realize(DEVICE(&ms->cpu[i]), NULL, &error_fatal);
    }

    qbus_init(&ms->periph_bus, sizeof(ms->periph_bus),
              TYPE_SYSTEM_BUS, DEVICE(&ms->cpu[0]), "esp32h4-periph-bus");

    qdev_init_gpio_in_named(DEVICE(&ms->cpu[0]), esp32h4_reset_request,
                           ESP32H4_RESET_GPIO_NAME, 1);

    /*
     * MachineState is not a DeviceState, so the core1 control inputs are plain
     * allocated IRQs rather than named qdev GPIOs.
     */
    qemu_irq core1_run_irq =
        qemu_allocate_irq(esp32h4_core1_run_request, ms, 0);
    qemu_irq core1_stall_irq =
        qemu_allocate_irq(esp32h4_core1_stall_request, ms, 0);
    qemu_irq core1_sw_reset_irq =
        qemu_allocate_irq(esp32h4_core1_sw_reset_request, ms, 0);

    for (int i = 0; i < ESP32H4_UART_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "uart%d", i);
        object_initialize_child(OBJECT(machine), name, &ms->uart[i], TYPE_ESP32H4_UART);
        snprintf(name, sizeof(name), "serial%d", i);
        object_property_add_alias(OBJECT(machine), name, OBJECT(&ms->uart[i]), "chardev");
        qdev_prop_set_chr(DEVICE(&ms->uart[i]), "chardev", serial_hd(i));
    }

    for (unsigned i = 0; i < ms->num_cpus; i++) {
        char name[24];
        snprintf(name, sizeof(name), "intmatrix%u", i);
        object_initialize_child(OBJECT(machine), name, &ms->intmatrix[i],
                                TYPE_ESP32H4_INTMATRIX);
    }
    object_initialize_child(OBJECT(machine), "intpri", &ms->intpri, TYPE_ESP32H4_INTPRI);
    object_initialize_child(OBJECT(machine), "gpio", &ms->gpio, TYPE_ESP32H4_GPIO);
    object_initialize_child(OBJECT(machine), "efuse", &ms->efuse, TYPE_ESP32H4_EFUSE);
    object_initialize_child(OBJECT(machine), "clock", &ms->clock, TYPE_ESP32H4_CLOCK);
    object_initialize_child(OBJECT(machine), "gdma", &ms->gdma, TYPE_ESP32H4_GDMA);
    object_initialize_child(OBJECT(machine), "sha", &ms->sha, TYPE_ESP32H4_SHA);
    object_initialize_child(OBJECT(machine), "timg0", &ms->timg[0], TYPE_ESP32H4_TIMG);
    object_initialize_child(OBJECT(machine), "timg1", &ms->timg[1], TYPE_ESP32H4_TIMG);
    for (int i = 0; i < 2; i++) {
        ESPTimgState *timg = ESP_TIMG(&ms->timg[i]);
        timg->xtal_freq = 32000000;
        timg->wdt_clk_is_xtal = true; /* H4 MWDT default source is XTAL via PCR */
        timg->cali_osc_freq[1] = 32768;
        timg->cali_osc_freq[2] = 32000;
        timg->cali_osc_freq[3] = 136000;
        timg->cali_osc_freq[4] = ESP_RC_FAST_FREQ / 256;
    }
    object_initialize_child(OBJECT(machine), "systimer", &ms->systimer, TYPE_ESP32H4_SYSTIMER);
    object_initialize_child(OBJECT(machine), "spi1", &ms->spi1, TYPE_ESP32H4_SPI);
    object_initialize_child(OBJECT(machine), "jtag", &ms->jtag, TYPE_ESP32H4_JTAG);
    object_initialize_child(OBJECT(machine), "pcr", &ms->pcr, TYPE_ESP32H4_PCR);
    object_initialize_child(OBJECT(machine), "lp", &ms->lp, TYPE_ESP32H4_LP);
    object_initialize_child(OBJECT(machine), "spi_mem", &ms->spi_mem, TYPE_ESP32H4_SPI_MEM);
    object_initialize_child(OBJECT(machine), "i2c_ana_mst", &ms->i2c_ana_mst, TYPE_ESP32H4_I2C_ANA_MST);
    object_initialize_child(OBJECT(machine), "modem_lpcon", &ms->modem, TYPE_ESP32H4_MODEM);

    /*
     * One interrupt matrix + CLIC per HP core. The matrices are mapped at
     * distinct addresses (core1's at INTMTX1) because IDF programs either
     * core's mapping registers from whichever core it happens to be running
     * on. The CLICs are the opposite: each core reaches its own at
     * DR_REG_CLIC_BASE and the other core's at DR_REG_CLIC_CORE1_BASE, so they
     * go into the per-hart address spaces rather than the shared system map.
     */
    const hwaddr intmatrix_base[ESP32H4_CPU_COUNT] = {
        DR_REG_INTERRUPT_BASE, DR_REG_INTERRUPT_CORE1_BASE
    };
    for (unsigned i = 0; i < ms->num_cpus; i++) {
        object_property_set_link(OBJECT(&ms->intmatrix[i]), "cpu",
                                 OBJECT(&ms->cpu[i]), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->intmatrix[i]), &error_fatal);

        MemoryRegion *map_mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix[i]), 0);
        memory_region_add_subregion_overlap(sys_mem, intmatrix_base[i],
                                            map_mr, 0);

        MemoryRegion *clic_mr =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intmatrix[i]), 1);

        for (unsigned view = 0; view < ms->num_cpus; view++) {
            const hwaddr base = (view == i) ? DR_REG_CLIC_BASE
                                            : DR_REG_CLIC_CORE1_BASE;
            MemoryRegion *mr;

            if (view == i) {
                mr = clic_mr;
            } else {
                char name[48];
                snprintf(name, sizeof(name), "esp32h4.clic%u-from-cpu%u",
                         i, view);
                mr = g_new(MemoryRegion, 1);
                memory_region_init_alias(mr, OBJECT(machine), name, clic_mr, 0,
                                         memory_region_size(clic_mr));
            }
            memory_region_add_subregion_overlap(&ms->cpu_mem[view], base, mr, 1);
        }

        for (int line = 0; line <= ESP32H4_CPU_INT_COUNT; line++) {
            qemu_irq cpu_input = qdev_get_gpio_in_named(DEVICE(&ms->cpu[i]),
                                                        ESP_CPU_IRQ_LINES_NAME,
                                                        line);
            qdev_connect_gpio_out_named(DEVICE(&ms->intmatrix[i]),
                                        ESP32H4_INT_MATRIX_OUTPUT_NAME,
                                        line, cpu_input);
        }
    }

    esp32h4_init_openeth(ms);

    /* USB Serial JTAG */
    {
        qdev_prop_set_chr(DEVICE(&ms->jtag), "chardev", serial_hd(ESP32H4_UART_COUNT));
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
            esp32h4_init_spi_flash(ms, blk);
        }
    }

    /* UARTs */
    {
        const hwaddr uart_base[] = { DR_REG_UART_BASE, DR_REG_UART1_BASE };
        for (int i = 0; i < ESP32H4_UART_COUNT; ++i) {
            sysbus_realize(SYS_BUS_DEVICE(&ms->uart[i]), &error_fatal);
            MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->uart[i]), 0);
            memory_region_add_subregion_overlap(sys_mem, uart_base[i], mr, 0);
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->uart[i]), 0,
                               esp32h4_intmatrix_in(ms, H4_ETS_UART0_INTR_SOURCE + i));
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
                           esp32h4_intmatrix_in(ms, H4_ETS_EFUSE_INTR_SOURCE));
    }

    /* PCR — also gates HP core1 */
    {
        ms->pcr.cpu_freq_changed = esp32h4_cpu_freq_changed;
        ms->pcr.cpu_freq_opaque = ms;
        sysbus_realize(SYS_BUS_DEVICE(&ms->pcr), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->pcr), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSCON_BASE, mr, 0);
        qdev_connect_gpio_out_named(DEVICE(&ms->pcr),
            ESP32H4_PCR_CORE1_RUN_GPIO, 0, core1_run_irq);
    }

    /* LP domain */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->lp), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->lp), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32H4_LP_BASE, mr, 0);
        qdev_connect_gpio_out_named(DEVICE(&ms->lp), ESP32H4_LP_RESET_GPIO, 0,
                                    qdev_get_gpio_in_named(DEVICE(&ms->cpu[0]),
                                        ESP32H4_RESET_GPIO_NAME, 0));
        qdev_connect_gpio_out_named(DEVICE(&ms->lp),
            ESP32H4_LP_CORE1_STALL_GPIO, 0, core1_stall_irq);
        qdev_connect_gpio_out_named(DEVICE(&ms->lp),
            ESP32H4_LP_CORE1_SW_RESET_GPIO, 0, core1_sw_reset_irq);
    }

    /* SPI0 MEM controller */
    {
        if (blk) {
            ms->spi_mem.parent.flash_blk = blk;
        }
        ms->spi_mem.parent.dcache_mr = &ms->cache.dcache;
        sysbus_realize(SYS_BUS_DEVICE(&ms->spi_mem), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->spi_mem), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32H4_SPI_MEM_BASE, mr, 0);
    }

    /* I2C Analog Master */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->i2c_ana_mst), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->i2c_ana_mst), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32H4_I2C_ANA_MST_BASE, mr, 0);
    }

    /* GDMA */
    {
        object_property_set_link(OBJECT(&ms->gdma), "soc_mr", OBJECT(dram), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ms->gdma), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->gdma), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GDMA_BASE, mr, 0);
        for (int i = 0; i < ESP32H4_GDMA_CHANNEL_COUNT; i++) {
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_IN_NAME, i,
                                        esp32h4_intmatrix_in(ms, H4_ETS_DMA_IN_CH0_INTR_SOURCE + i));
            qdev_connect_gpio_out_named(DEVICE(&ms->gdma), ESP_GDMA_IRQ_OUT_NAME, i,
                                        esp32h4_intmatrix_in(ms, H4_ETS_DMA_OUT_CH0_INTR_SOURCE + i));
        }
    }

    /* SHA */
    {
        ms->sha.parent.parent.gdma = ESP_GDMA(&ms->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ms->sha), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->sha), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SHA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ms->sha), 0,
                           esp32h4_intmatrix_in(ms, H4_ETS_SHA_INTR_SOURCE));
    }

    /* MODEM_LPCON */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->modem), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->modem), 0);
        memory_region_add_subregion_overlap(sys_mem, ESP32H4_MODEM_BASE, mr, 0);
    }

    /* System clock, plus the HP_SYSTEM words the C3 window swallows */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->clock), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->clock), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTEM_BASE, mr, 0);
        /* APPCPU_BOOT_ADDR — ROM core1 polls this */
        memory_region_add_subregion_overlap(sys_mem, DR_REG_HP_SYSTEM_BASE + 0x84,
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->clock), 1), 1);
        /* CORE_DEBUG_RUNSTALL_CONF — unstall spins on CORE1_RUNSTALLED */
        memory_region_init_io(&ms->runstall_mmio, OBJECT(ms),
                              &esp32h4_runstall_ops, ms,
                              "esp32h4.hp_system.runstall", 4);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_HP_SYSTEM_BASE + 0x40,
                                            &ms->runstall_mmio, 1);
    }

    /* INTPRI */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->intpri), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->intpri), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTPRI_BASE, mr, 0);

        for (int i = 0; i < ESP32H4_INTPRI_CPU_INTR_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->intpri), i,
                               esp32h4_intmatrix_in(ms, H4_ETS_FROM_CPU_INTR0_SOURCE + i));
        }
    }

    /* Timer Groups */
    for (int tg = 0; tg < 2; tg++) {
        sysbus_realize(SYS_BUS_DEVICE(&ms->timg[tg]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->timg[tg]), 0);
        const hwaddr timg_base[] = { DR_REG_TIMERGROUP0_BASE, DR_REG_TIMERGROUP1_BASE };
        memory_region_add_subregion_overlap(sys_mem, timg_base[tg], mr, 0);

        const int tg0_src[] = { H4_ETS_TG0_T0_LEVEL_INTR_SOURCE, H4_ETS_TG0_WDT_LEVEL_INTR_SOURCE };
        const int tg1_src[] = { H4_ETS_TG1_T0_LEVEL_INTR_SOURCE, H4_ETS_TG1_WDT_LEVEL_INTR_SOURCE };
        const int *src = tg == 0 ? tg0_src : tg1_src;

        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32H4_T0_IRQ_INTERRUPT, 0,
                                    esp32h4_intmatrix_in(ms, src[0]));
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32H4_WDT_IRQ_INTERRUPT, 0,
                                    esp32h4_intmatrix_in(ms, src[1]));

        const int wdt_reset_reason = tg == 0 ? ESP32H4_CORE_MWDT0 : ESP32H4_CORE_MWDT1;
        qdev_connect_gpio_out_named(DEVICE(&ms->timg[tg]), ESP32H4_WDT_IRQ_RESET, 0,
                                    qdev_get_gpio_in(DEVICE(&ms->lp), wdt_reset_reason));
    }

    /* System timer */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ms->systimer), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ms->systimer), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTIMER_BASE, mr, 0);
        for (int i = 0; i < ESP_SYSTIMER_IRQ_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ms->systimer), i,
                               esp32h4_intmatrix_in(ms, H4_ETS_SYSTIMER_TARGET0_INTR_SOURCE + i));
        }
    }

    create_unimplemented_device("esp32h4.peripherals", ESP32H4_IO_START_ADDR, 0x100000);
    create_unimplemented_device("esp32h4.clic_area", 0x20000000, 0x1000000);
}

static void esp32h4_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Espressif ESP32-H4 machine";
    mc->default_cpu_type = TYPE_ESP_RISCV_CPU;
    mc->init = esp32h4_machine_init;
    /* Both HP cores; -smp 1 brings up core0 only */
    mc->min_cpus = 1;
    mc->max_cpus = ESP32H4_CPU_COUNT;
    mc->default_cpus = ESP32H4_CPU_COUNT;
    mc->default_ram_size = 320 * 1024;
}

static const TypeInfo esp32h4_info = {
    .name = TYPE_ESP32H4_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Esp32H4MachineState),
    .class_init = esp32h4_machine_class_init,
};

static void esp32h4_machine_type_init(void)
{
    type_register_static(&esp32h4_info);
}

type_init(esp32h4_machine_type_init);

/*
 * ESP32S3 SoC and Machine
 *
 * Copyright (c) 2023-2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/memalign.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/xtensa/xtensa_memory.h"
#include "hw/misc/unimp.h"
#include "hw/irq.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "target/xtensa/cpu.h"

#include "hw/misc/esp32s3_rtc_cntl.h"
#include "hw/xtensa/esp32s3_intc.h"

#include "hw/sd/dwc_sdmmc.h"
#include "hw/misc/ssi_psram.h"
#include "core-esp32s3/core-isa.h"
#include "qemu/datadir.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "exec/exec-all.h"
#include "net/net.h"
#include "elf.h"

#include "hw/ssi/esp32s3_spi.h"
#include "hw/misc/esp32s3_cache.h"
#include "hw/char/esp32s3_uart.h"
#include "hw/misc/esp32s3_rng.h"

#include "hw/nvram/esp32s3_efuse.h"
#include "hw/xtensa/esp32s3_clk.h"
#include "hw/dma/esp32s3_gdma.h"
#include "hw/misc/esp32s3_sha.h"
#include "hw/misc/esp32s3_aes.h"
#include "hw/misc/esp32s3_rsa.h"
#include "hw/misc/esp32s3_hmac.h"
#include "hw/misc/esp32s3_ds.h"
#include "hw/misc/esp32s3_app_trace.h"
#include "hw/timer/esp32s3_timg.h"
#include "hw/timer/esp32s3_systimer.h"
#include "hw/gpio/esp32s3_gpio.h"
#include "hw/misc/esp32s3_xts_aes.h"
#include "hw/misc/esp32s3_pms.h"
#include "hw/net/can/esp32s3_twai.h"

#include "cpu_esp32s3.h"

#include "hw/misc/esp32c3_jtag.h"
#include "hw/display/esp_rgb.h"

#define TYPE_ESP32S3_SOC "xtensa.esp32s3"
#define ESP32S3_SOC(obj) OBJECT_CHECK(Esp32s3SocState, (obj), TYPE_ESP32S3_SOC)

#define TYPE_ESP32S3_CPU XTENSA_CPU_TYPE_NAME("esp32s3")


enum {
    ESP32S3_MEMREGION_IROM,
    ESP32S3_MEMREGION_DROM,
    ESP32S3_MEMREGION_DRAM,
    ESP32S3_MEMREGION_IRAM,
    ESP32S3_MEMREGION_ICACHE,
    ESP32S3_MEMREGION_DCACHE,
    ESP32S3_MEMREGION_RTCSLOW,
    ESP32S3_MEMREGION_RTCFAST,
    ESP32S3_MEMREGION_FRAMEBUF,
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp32s3_memmap[] = {
    [ESP32S3_MEMREGION_DROM] = { 0x3ff00000, 0x20000 },
    [ESP32S3_MEMREGION_IROM] = { 0x40000000, 0x60000 },
    [ESP32S3_MEMREGION_DRAM] = { 0x3FC80000, 0x170000 },
    [ESP32S3_MEMREGION_IRAM] = { 0x40370000, 0x80000 },
    [ESP32S3_MEMREGION_DCACHE] = { 0x3c000000, ESP32S3_EXTMEM_REGION_SIZE },
    [ESP32S3_MEMREGION_ICACHE] = { 0x42000000, ESP32S3_EXTMEM_REGION_SIZE },
    [ESP32S3_MEMREGION_RTCSLOW] = { 0x50000000, 0x2000 },
    [ESP32S3_MEMREGION_RTCFAST] = { 0x600fe000, 0x2000 },
    /* Virtual Framebuffer, used for the graphical interface */
    [ESP32S3_MEMREGION_FRAMEBUF] = { 0x20000000, ESP_RGB_MAX_VRAM_SIZE },
};


#define ESP32S3_SOC_RESET_PROCPU    0x1
#define ESP32S3_SOC_RESET_APPCPU    0x2
#define ESP32S3_SOC_RESET_PERIPH    0x4
#define ESP32S3_SOC_RESET_DIG       (ESP32S3_SOC_RESET_PROCPU | ESP32S3_SOC_RESET_APPCPU | ESP32S3_SOC_RESET_PERIPH)
#define ESP32S3_SOC_RESET_RTC       0x8
#define ESP32S3_SOC_RESET_ALL       (ESP32S3_SOC_RESET_RTC | ESP32S3_SOC_RESET_DIG)

#define ESP32S3_IO_WARNING  0

typedef struct Esp32s3SocState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    XtensaCPU cpu[ESP32S3_CPU_COUNT];
    Esp32s3IntMatrixState intmatrix;
    ESP32S3UARTState uart[ESP32S3_UART_COUNT];
    ESP32S3GPIOState gpio;
    Esp32s3RngState rng;
    Esp32S3TWAIState twai;

    Esp32s3RtcCntlState rtc_cntl;

    BusState rtc_bus;
    BusState periph_bus;

    MemoryRegion cpu_specific_mem[ESP32S3_CPU_COUNT];
    ESP32S3SpiState spi1;
    ESP32S3CacheState cache;
    ESP32S3EfuseState efuse;
    ESP32S3ClockState clock;
    ESP32S3GdmaState gdma;
    ESP32S3ShaState sha;
    ESP32S3AesState aes;
    ESP32S3RsaState rsa;
    ESP32S3HmacState hmac;
    ESP32S3DsState ds;
    ESP32S3PmsState pms;

    ESP32S3XtsAesState xts_aes;
    ESP32S3TimgState timg[2];
    ESP32S3SysTimerState systimer;

    ESP32C3UsbJtagState jtag;
    ESPRgbState rgb;

    MemoryRegion iomem;
    DWCSDMMCState sdmmc;
    DeviceState *eth;
    SsiPsramState *psram;

    uint32_t requested_reset;
} Esp32s3SocState;


/* Temporary macro to mark the CPU as in non-debugging mode */
#define A_ASSIST_DEBUG_CORE_0_DEBUG_MODE_REG    0x098

/* "QEMU" as a 32-bit value, can be used by the application to to check whether it is running in
 * QEMU or on real hardware */
#define RGB_QEMU_ORIGIN     0x51454d55
#define RGB_QEMU_ORIGIN_REG 0x3F8

static void remove_cpu_watchpoints(XtensaCPU* xcs)
{
    for (int i = 0; i < MAX_NDBREAK; ++i) {
        if (xcs->env.cpu_watchpoint[i]) {
            cpu_watchpoint_remove_by_ref(CPU(xcs), xcs->env.cpu_watchpoint[i]);
            xcs->env.cpu_watchpoint[i] = NULL;
        }
    }
}

static void esp32s3_dig_reset(void *opaque, int n, int level)
{
    Esp32s3SocState *s = ESP32S3_SOC(opaque);
    if (level) {
        s->requested_reset = ESP32S3_SOC_RESET_DIG;
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void esp32s3_cpu_reset(void* opaque, int n, int level)
{
    Esp32s3SocState *s = ESP32S3_SOC(opaque);
    if (level) {
        s->requested_reset = (n == 0) ? ESP32S3_SOC_RESET_PROCPU : ESP32S3_SOC_RESET_APPCPU;
        /* Use different cause for APP CPU so that its reset doesn't cause QEMU to exit,
         * when -no-reboot option is given.
         */
        ShutdownCause cause = (n == 0) ? SHUTDOWN_CAUSE_GUEST_RESET : SHUTDOWN_CAUSE_SUBSYSTEM_RESET;
        qemu_system_reset_request(cause);
    }
}

static void esp32s3_soc_reset(DeviceState *dev)
{
    Esp32s3SocState *s = ESP32S3_SOC(dev);
    if (s->requested_reset == 0) {
        s->requested_reset = ESP32S3_SOC_RESET_ALL;
    }
    if (s->requested_reset & ESP32S3_SOC_RESET_PERIPH) {
        device_cold_reset(DEVICE(&s->intmatrix));
        for (int i = 0; i < ESP32S3_UART_COUNT; ++i) {
            device_cold_reset(DEVICE(&s->uart[i]));
        }
    }
    if (s->requested_reset & ESP32S3_SOC_RESET_PROCPU) {
        xtensa_select_static_vectors(&s->cpu[0].env, s->rtc_cntl.stat_vector_sel[0]);
        remove_cpu_watchpoints(&s->cpu[0]);
        cpu_reset(CPU(&s->cpu[0]));
    }
    if (s->requested_reset & ESP32S3_SOC_RESET_APPCPU && (ESP32S3_CPU_COUNT > 1)) {
        xtensa_select_static_vectors(&s->cpu[1].env, s->rtc_cntl.stat_vector_sel[1]);
        remove_cpu_watchpoints(&s->cpu[1]);
        cpu_reset(CPU(&s->cpu[1]));
    }
    s->requested_reset = 0;
}

static void esp32s3_cpu_stall(void* opaque, int n, int level)
{
}

static void esp32s3_clk_update(void* opaque, int n, int level)
{
    if (!level) {
        return;
    }
}

static void esp32s3_soc_add_periph_device(MemoryRegion *dest, void* dev, hwaddr dport_base_addr)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion_overlap(dest, dport_base_addr, mr, 0);
    MemoryRegion *mr_apb = g_new(MemoryRegion, 1);
    char *name = g_strdup_printf("mr-apb-0x%08x", (uint32_t) dport_base_addr);
    memory_region_init_alias(mr_apb, OBJECT(dev), name, mr, 0, memory_region_size(mr));
    g_free(name);
}

#define MB (1024*1024)

static void esp32s3_init_spi_flash(Esp32s3SocState *ms, BlockBackend* blk)
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

    /* Realize the SPI flash, its "drive" (blk) property must already be set! */
    qdev_realize(flash_dev, spi_bus, &error_fatal);
    qdev_connect_gpio_out_named(spi_master, SSI_GPIO_CS, 0,
                                qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0));
}

static void esp32s3_machine_init_psram(Esp32s3SocState *ms, uint32_t size_mbytes)
{
    /* PSRAM attached to SPI1, CS1 */
    DeviceState *spi_master = DEVICE(&ms->spi1);
    BusState* spi_bus = qdev_get_child_bus(spi_master, "spi");
    DeviceState *psram = qdev_new(TYPE_SSI_PSRAM);
    qdev_prop_set_uint32(psram, "size_mbytes", size_mbytes);
    qdev_prop_set_uint8(psram, "cs", 1);
    qdev_realize(psram, spi_bus, &error_fatal);
    ms->psram = SSI_PSRAM(psram);
    qdev_connect_gpio_out_named(spi_master, SSI_GPIO_CS, 1,
                                qdev_get_gpio_in_named(psram, SSI_GPIO_CS, 0));
}

static void esp32s3_machine_init_sd(Esp32s3SocState* ss)
{
    DriveInfo *dinfo = drive_get(IF_SD, 0, 0);
    if (dinfo) {
        DeviceState *card;

        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        /* See the comment on not using sysbus-default in esp32_machine_init_i2c */
        DeviceState *sdmmc = DEVICE(&ss->sdmmc);
        SDBus* sd_bus = SD_BUS(qdev_get_child_bus(sdmmc, "sd-bus"));
        qdev_realize_and_unref(card, BUS(sd_bus), &error_fatal);
    }
}

struct Esp32s3MachineState {
    MachineState parent;

    Esp32s3SocState esp32s3;
    DeviceState *flash_dev;
    char *app_trace_mode;    /* "file_io", or NULL when disabled */
    char *app_trace_path;    /* sandbox root for AppTrace files, or NULL */
};
#define TYPE_ESP32S3_MACHINE MACHINE_TYPE_NAME("esp32s3")
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3MachineState, ESP32S3_MACHINE)

static void esp32s3_init_openeth(Esp32s3SocState *ms)
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


static void esp32s3_soc_realize(DeviceState *dev, Error **errp)
{
    Esp32s3SocState *s = ESP32S3_SOC(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    DeviceState* intmatrix_dev = DEVICE(&s->intmatrix);
    MemoryRegion *sys_mem = get_system_memory();

    const struct MemmapEntry *memmap = esp32s3_memmap;

    MemoryRegion *iram = g_new(MemoryRegion, 1);
    MemoryRegion *rtcslow = g_new(MemoryRegion, 1);
    MemoryRegion *rtcfast = g_new(MemoryRegion, 1);

    for (int i = 0; i < ms->smp.cpus; ++i) {
        MemoryRegion *drom = g_new(MemoryRegion, 1);
        MemoryRegion *irom = g_new(MemoryRegion, 1);

        char name[20];
        snprintf(name, sizeof(name), "esp32s3.irom.cpu%d", i);
        memory_region_init_rom(irom, NULL, name, memmap[ESP32S3_MEMREGION_IROM].size, &error_fatal);
        memory_region_add_subregion(&s->cpu_specific_mem[i], memmap[ESP32S3_MEMREGION_IROM].base, irom);

        const hwaddr offset_in_orig = 0x40000;
        snprintf(name, sizeof(name), "esp32s3.drom.cpu%d", i);
        memory_region_init_alias(drom, NULL, name, irom, offset_in_orig, memmap[ESP32S3_MEMREGION_DROM].size);
        memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_DROM].base, drom);
    }


    memory_region_init_ram(iram, NULL, "esp32s3.iram",
                           memmap[ESP32S3_MEMREGION_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_IRAM].base, iram);

    memory_region_init_ram(rtcslow, NULL, "esp32s3.rtcslow",
                           memmap[ESP32S3_MEMREGION_RTCSLOW].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_RTCSLOW].base, rtcslow);

    memory_region_init_ram(rtcfast, NULL, "esp32s3.rtcfast",
                           memmap[ESP32S3_MEMREGION_RTCFAST].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_RTCFAST].base, rtcfast);

    for (int i = 0; i < ms->smp.cpus; ++i) {
        qdev_realize(DEVICE(&s->cpu[i]), NULL, &error_fatal);
    }

    Esp32s3MachineState *m3 = ESP32S3_MACHINE(ms);
    if (m3->app_trace_mode) {
        Esp32s3AppTraceState *app_trace =
            esp32s3_app_trace_new(m3->app_trace_mode, m3->app_trace_path, errp);
        if (!app_trace) {
            return;
        }
        object_property_add_child(OBJECT(s), "app_trace", OBJECT(app_trace));
        object_unref(OBJECT(app_trace));
        for (int i = 0; i < ms->smp.cpus; ++i) {
            xtensa_cpu_set_er_ops(&s->cpu[i].env,
                                  esp32s3_app_trace_er_read,
                                  esp32s3_app_trace_er_write,
                                  esp32s3_app_trace_core_context(app_trace, i));
        }
    }

    for (int i = 0; i < ESP32S3_CPU_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "cpu%d", i);
        object_property_set_link(OBJECT(&s->intmatrix), name, OBJECT(qemu_get_cpu(i)), &error_abort);
    }
    qdev_realize(DEVICE(&s->intmatrix), &s->periph_bus, &error_fatal);


    qdev_realize(DEVICE(&s->rtc_cntl), &s->rtc_bus, &error_fatal);
    esp32s3_soc_add_periph_device(sys_mem, &s->rtc_cntl, DR_REG_RTCCNTL_BASE);

    qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_DIG_RESET_GPIO, 0,
                                qdev_get_gpio_in_named(dev, ESP32S3_RTC_DIG_RESET_GPIO, 0));
    qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_CLK_UPDATE_GPIO, 0,
                                qdev_get_gpio_in_named(dev, ESP32S3_RTC_CLK_UPDATE_GPIO, 0));
    for (int i = 0; i < ms->smp.cpus; ++i) {
        qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_CPU_RESET_GPIO, i,
                                    qdev_get_gpio_in_named(dev, ESP32S3_RTC_CPU_RESET_GPIO, i));
        qdev_connect_gpio_out_named(DEVICE(&s->rtc_cntl), ESP32S3_RTC_CPU_STALL_GPIO, i,
                                    qdev_get_gpio_in_named(dev, ESP32S3_RTC_CPU_STALL_GPIO, i));
    }

    for (int i = 0; i < ESP32S3_UART_COUNT; ++i) {
        const hwaddr uart_base[] = {DR_REG_UART_BASE, DR_REG_UART1_BASE, DR_REG_UART2_BASE};
        qdev_realize(DEVICE(&s->uart[i]), &s->periph_bus, &error_fatal);
        esp32s3_soc_add_periph_device(sys_mem, &s->uart[i], uart_base[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_UART0_INTR_SOURCE + i));
    }

    qdev_realize(DEVICE(&s->sdmmc), &s->periph_bus, &error_fatal);
    esp32s3_soc_add_periph_device(sys_mem, &s->sdmmc, DR_REG_SDMMC_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdmmc), 0,
                       qdev_get_gpio_in(intmatrix_dev, ETS_SDIO_HOST_INTR_SOURCE));

    /* Emulation of APB_CTRL_DATE_REG, needed for ECO3 revision detection.
     * This is a small hack to avoid creating a whole new device just to emulate one
     * register.
     */
    const hwaddr apb_ctrl_regs = DR_REG_APB_CTRL_BASE;
    MemoryRegion *apbctrl_mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(apbctrl_mem, NULL, "esp32s3.apbctrl", 0x400 /* bytes */, &error_fatal);
    memory_region_add_subregion(sys_mem, apb_ctrl_regs, apbctrl_mem);
    uint32_t apb_ctrl_date_reg_val = 0x16042000 | 0x80000000;  /* MSB indicates ECO3 silicon revision */
    uint32_t qemu_sig = RGB_QEMU_ORIGIN;
    cpu_physical_memory_write(apb_ctrl_regs + 0x7c, &apb_ctrl_date_reg_val, 4);
    cpu_physical_memory_write(apb_ctrl_regs + RGB_QEMU_ORIGIN_REG, &qemu_sig, 4);

    qemu_register_reset((QEMUResetHandler*) esp32s3_soc_reset, dev);

    /* TWAI realization */
    {
        /* Initialize and realize the TWAI device */
        sysbus_realize(SYS_BUS_DEVICE(&s->twai), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->twai), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TWAI_BASE, mr, 0);
        /* Connect TWAI interrupt to the interrupt matrix */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->twai), 0,
                          qdev_get_gpio_in(intmatrix_dev, ETS_TWAI_INTR_SOURCE));
    }
}


static uint64_t esp32s3_io_read(void *opaque, hwaddr addr, unsigned int size)
{
#if ESP32S3_IO_WARNING
    warn_report("[ESP32-S3] Unsupported read to $%08lx, size = %i\n", ESP32S3_IO_START_ADDR + addr, size);
#endif
    return 0;
}


static void esp32s3_io_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
#if ESP32S3_IO_WARNING
        warn_report("[ESP32-S3] Unsupported write $%08lx = %08lx\n", ESP32S3_IO_START_ADDR + addr, value);
#endif
}


/* Define operations for I/OS */
static const MemoryRegionOps esp32s3_io_ops = {
    .read =  esp32s3_io_read,
    .write = esp32s3_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};



static void esp32s3_soc_init(Object *obj)
{
    Esp32s3SocState *s = ESP32S3_SOC(obj);
    MachineState *ms = MACHINE(qdev_get_machine());
    char name[16];
    MemoryRegion *system_memory = get_system_memory();


    qbus_init(&s->periph_bus, sizeof(s->periph_bus),
                        TYPE_SYSTEM_BUS, DEVICE(s), "esp32-periph-bus");
    qbus_init(&s->rtc_bus, sizeof(s->rtc_bus),
                        TYPE_SYSTEM_BUS, DEVICE(s), "esp32-rtc-bus");

    for (int i = 0; i < ms->smp.cpus; ++i) {
        snprintf(name, sizeof(name), "cpu%d", i);

        object_initialize_child(obj, name, &s->cpu[i], TYPE_ESP32S3_CPU);
        // Allocate memory for TIE registers
        s->cpu[i].env.ext = qemu_memalign(16, sizeof(CPUXtensaEsp32s3State));

        if (i == 0)
        {
            s->cpu[i].env.sregs[PRID] = 0xcdcd;
        }
        if (i == 1)
        {
            s->cpu[i].env.sregs[PRID] = 0xabab;
        }

        snprintf(name, sizeof(name), "cpu%d-mem", i);
        memory_region_init(&s->cpu_specific_mem[i], NULL, name, UINT32_MAX);

        CPUState* cs = CPU(&s->cpu[i]);
        cs->num_ases = 1;
        cpu_address_space_init(cs, 0, "cpu-memory", &s->cpu_specific_mem[i]);

        MemoryRegion *cpu_view_sysmem = g_new(MemoryRegion, 1);
        snprintf(name, sizeof(name), "cpu%d-sysmem", i);
        memory_region_init_alias(cpu_view_sysmem, NULL, name, system_memory, 0, UINT32_MAX);
        memory_region_add_subregion_overlap(&s->cpu_specific_mem[i], 0, cpu_view_sysmem, 0);
        cs->memory = &s->cpu_specific_mem[i];
    }

    for (int i = 0; i < ESP32S3_UART_COUNT; ++i) {
        snprintf(name, sizeof(name), "uart%d", i);
        object_initialize_child(obj, name, &s->uart[i], TYPE_ESP32S3_UART);
    }

    object_property_add_alias(obj, "serial0", OBJECT(&s->uart[0]), "chardev");
    object_property_add_alias(obj, "serial1", OBJECT(&s->uart[1]), "chardev");
    // object_property_add_alias(obj, "serial2", OBJECT(&s->uart[2]), "chardev");
    qdev_prop_set_chr(DEVICE(&s->uart[0]), "chardev", serial_hd(0));
    qdev_prop_set_chr(DEVICE(&s->uart[1]), "chardev", serial_hd(1));
    // qdev_prop_set_chr(DEVICE(&s->uart[2]), "chardev", serial_hd(2));

    object_initialize_child(obj, "intmatrix", &s->intmatrix, TYPE_ESP32S3_INTMATRIX);

    object_initialize_child(obj, "rtc_cntl", &s->rtc_cntl, TYPE_ESP32S3_RTC_CNTL);

    qdev_init_gpio_in_named(DEVICE(s), esp32s3_dig_reset,  ESP32S3_RTC_DIG_RESET_GPIO, 1);
    qdev_init_gpio_in_named(DEVICE(s), esp32s3_cpu_reset,  ESP32S3_RTC_CPU_RESET_GPIO, ESP32S3_CPU_COUNT);
    qdev_init_gpio_in_named(DEVICE(s), esp32s3_cpu_stall,  ESP32S3_RTC_CPU_STALL_GPIO, ESP32S3_CPU_COUNT);
    qdev_init_gpio_in_named(DEVICE(s), esp32s3_clk_update, ESP32S3_RTC_CLK_UPDATE_GPIO, 1);

    object_initialize_child(obj, "twai", &s->twai, TYPE_ESP32S3_TWAI);

    object_initialize_child(obj, "sdmmc", &s->sdmmc, TYPE_DWC_SDMMC);
}

static Property esp32s3_soc_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32s3_soc_realize;
    device_class_set_props(dc, esp32s3_soc_properties);
}

static const TypeInfo esp32s3_soc_info = {
    .name = TYPE_ESP32S3_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Esp32s3SocState),
    .instance_init = esp32s3_soc_init,
    .class_init = esp32s3_soc_class_init
};

static void esp32s3_soc_register_types(void)
{
    type_register_static(&esp32s3_soc_info);
}

type_init(esp32s3_soc_register_types)


static uint64_t translate_phys_addr(void *opaque, uint64_t addr)
{
    XtensaCPU *cpu = opaque;

    return cpu_get_phys_page_debug(CPU(cpu), addr);
}

// -----------------------------------------------

static void esp32s3_soc_add_unimp_device(MemoryRegion *dest, const char* name, hwaddr dport_base_addr, size_t size)
{
    create_unimplemented_device(name, dport_base_addr, size);
    char * name_apb = g_strdup_printf("%s-apb", name);
    create_unimplemented_device(name_apb, dport_base_addr + APB_REG_BASE, size);
    g_free(name_apb);
}

static void esp32s3_machine_init(MachineState *machine)
{
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    BlockBackend* blk = NULL;
    if (dinfo) {
        /* MTD was given! We need to initialize and emulate SPI flash */
        qemu_log("Adding SPI flash device\n");
        blk = blk_by_legacy_dinfo(dinfo);
    } else {
        qemu_log("Not initializing SPI Flash\n");
    }

    MemoryRegion *sys_mem = get_system_memory();
    Esp32s3MachineState *ms = ESP32S3_MACHINE(machine);
    object_initialize_child(OBJECT(ms), "soc", &ms->esp32s3, TYPE_ESP32S3_SOC);
    Esp32s3SocState *ss = ESP32S3_SOC(&ms->esp32s3);

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    const struct MemmapEntry *memmap = esp32s3_memmap;

    memory_region_init_ram(dram, NULL, "esp32s3.dram",
                           memmap[ESP32S3_MEMREGION_DRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_DRAM].base, dram);


    memory_region_init_io(&ss->iomem, OBJECT(&ss->cpu[0]), &esp32s3_io_ops,
                          NULL, "esp32s3.iomem", 0xd1000);
    memory_region_add_subregion(sys_mem, ESP32S3_IO_START_ADDR, &ss->iomem);

    // qdev_prop_set_chr(DEVICE(ss), "serial0", serial_hd(0));
    // qdev_prop_set_chr(DEVICE(ss), "serial1", serial_hd(1));
    // qdev_prop_set_chr(DEVICE(ss), "serial2", serial_hd(2));

    qdev_realize(DEVICE(ss), NULL, &error_fatal);

    object_initialize_child(OBJECT(ss), "extmem", &ss->cache, TYPE_ESP32S3_CACHE);
    object_initialize_child(OBJECT(ss), "spi1", &ss->spi1, TYPE_ESP32S3_SPI);
    object_initialize_child(OBJECT(ss), "efuse", &ss->efuse, TYPE_ESP32S3_EFUSE);
    object_initialize_child(OBJECT(ss), "jtag", &ss->jtag, TYPE_ESP32C3_JTAG);
    object_initialize_child(OBJECT(ss), "gpio", &ss->gpio, TYPE_ESP32S3_GPIO);
    object_initialize_child(OBJECT(ss), "rng", &ss->rng, TYPE_ESP32S3_RNG);

    object_initialize_child(OBJECT(ss), "clock", &ss->clock, TYPE_ESP32S3_CLOCK);

    object_initialize_child(OBJECT(ss), "gdma", &ss->gdma, TYPE_ESP32S3_GDMA);
    object_initialize_child(OBJECT(ss), "sha", &ss->sha, TYPE_ESP32S3_SHA);
    object_initialize_child(OBJECT(ss), "aes", &ss->aes, TYPE_ESP32S3_AES);
    object_initialize_child(OBJECT(ss), "rsa", &ss->rsa, TYPE_ESP32S3_RSA);
    object_initialize_child(OBJECT(ss), "hmac", &ss->hmac, TYPE_ESP32S3_HMAC);
    object_initialize_child(OBJECT(ss), "ds", &ss->ds, TYPE_ESP32S3_DS);
    object_initialize_child(OBJECT(ss), "pms", &ss->pms, TYPE_ESP32S3_PMS);

    object_initialize_child(OBJECT(ss), "xts_aes", &ss->xts_aes, TYPE_ESP32S3_XTS_AES);
    object_initialize_child(OBJECT(ss), "timg0", &ss->timg[0], TYPE_ESP32S3_TIMG);
    object_initialize_child(OBJECT(ss), "timg1", &ss->timg[1], TYPE_ESP32S3_TIMG);
    object_initialize_child(OBJECT(ss), "systimer", &ss->systimer, TYPE_ESP32S3_SYSTIMER);
    object_initialize_child(OBJECT(ss), "rgb", &ss->rgb, TYPE_ESP_RGB);

    DeviceState* intmatrix_dev = DEVICE(&ss->intmatrix);
    {
        /* Store the current Machine CPU in the interrupt matrix */
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->intmatrix), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_INTERRUPT_BASE, mr, 0);
    }

    /* Initialize OpenCores Ethernet controller now sicne it requires the interrupt matrix */
    esp32s3_init_openeth(ss);

    /* USB Serial JTAG realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->jtag), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->jtag), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_USB_SERIAL_JTAG_BASE, mr, 0);
    }

    /* SPI1 controller (SPI Flash) */
    {
        ss->spi1.xts_aes = &ss->xts_aes;
        sysbus_realize(SYS_BUS_DEVICE(&ss->spi1), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->spi1), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SPI1_BASE, mr, 0);
        if (blk) {
            esp32s3_init_spi_flash(ss, blk);
        }
        if (machine->ram_size > 0) {
            esp32s3_machine_init_psram(ss, (uint32_t) (machine->ram_size / MiB));
        }
    }

    /* (Extmem) Cache realization */
    {
        if (blk) {
            ss->cache.flash_blk = blk;
        }
        if (ss->psram) {
            ss->cache.psram = ss->psram;
        }
        ss->cache.xts_aes = &ss->xts_aes;
        sysbus_realize(SYS_BUS_DEVICE(&ss->cache), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->cache), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EXTMEM_BASE, mr, 0);

        memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_DCACHE].base, &ss->cache.dcache);
        memory_region_add_subregion(sys_mem, memmap[ESP32S3_MEMREGION_ICACHE].base, &ss->cache.icache);
    }

    /* eFuses realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->efuse), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->efuse), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_EFUSE_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->efuse), 0,
                       qdev_get_gpio_in(intmatrix_dev, ETS_EFUSE_INTR_SOURCE));
    }

    /* System clock realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->clock), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->clock), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTEM_BASE, mr, 0);
        /* Connect the IRQ lines to the interrupt matrix */
        for (int i = 0; i < ESP32S3_SYSTEM_CPU_INTR_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ss->clock), i,
                           qdev_get_gpio_in(intmatrix_dev, ETS_FROM_CPU_INTR0_SOURCE + i));
        }
    }
    /* Timer Groups realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->timg[0]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->timg[0]), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TIMERGROUP0_BASE, mr, 0);
        /* Connect the T0 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[0]), ESP32S3_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_T0_LEVEL_INTR_SOURCE));
        /* Connect the T1 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[0]), ESP32S3_T1_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_T1_LEVEL_INTR_SOURCE));
        /* Connect the Watchdog interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[0]), ESP32S3_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG0_WDT_LEVEL_INTR_SOURCE));
    }
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->timg[1]), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->timg[1]), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_TIMERGROUP1_BASE, mr, 0);
        /* Connect the T0 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[1]), ESP32S3_T0_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_T0_LEVEL_INTR_SOURCE));
        /* Connect the T1 interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[1]), ESP32S3_T1_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_T1_LEVEL_INTR_SOURCE));
        /* Connect the Watchdog interrupt line to the interrupt matrix */
        qdev_connect_gpio_out_named(DEVICE(&ss->timg[1]), ESP32S3_WDT_IRQ_INTERRUPT, 0,
                                    qdev_get_gpio_in(intmatrix_dev, ETS_TG1_WDT_LEVEL_INTR_SOURCE));
    }

    /* System timer */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->systimer), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->systimer), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SYSTIMER_BASE, mr, 0);
        for (int i = 0; i < ESP_SYSTIMER_IRQ_COUNT; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&ss->systimer), i,
                           qdev_get_gpio_in(intmatrix_dev, ETS_SYSTIMER_TARGET0_EDGE_INTR_SOURCE + i));
        }
    }


    /* GPIO realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->gpio), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->gpio), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GPIO_BASE, mr, 0);
    }

    {
        qdev_realize(DEVICE(&ss->rng), &ss->periph_bus, &error_fatal);
        esp32s3_soc_add_periph_device(sys_mem, &ss->rng, ESP32S3_RNG_BASE);

    }


    /* GDMA Realization */
    {
        object_property_set_link(OBJECT(&ss->gdma), "soc_mr", OBJECT(dram), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&ss->gdma), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->gdma), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_GDMA_BASE, mr, 0);
        /* Connect the IRQs to the Interrupt Matrix */
        for (int i = 0; i < ESP32S3_GDMA_CHANNEL_COUNT; i++) {
            qdev_connect_gpio_out_named(DEVICE(&ss->gdma), ESP_GDMA_IRQ_IN_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, ETS_DMA_IN_CH0_INTR_SOURCE + i));
            qdev_connect_gpio_out_named(DEVICE(&ss->gdma), ESP_GDMA_IRQ_OUT_NAME, i,
                                        qdev_get_gpio_in(intmatrix_dev, ETS_DMA_OUT_CH0_INTR_SOURCE + i));
        }
   }

    /* SHA realization */
    {
        ss->sha.parent.gdma = ESP_GDMA(&ss->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ss->sha), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->sha), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SHA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->sha), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_SHA_INTR_SOURCE));
    }

    /* AES realization */
    {
        ss->aes.parent.gdma = ESP_GDMA(&ss->gdma);
        sysbus_realize(SYS_BUS_DEVICE(&ss->aes), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->aes), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_AES_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->aes), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_AES_INTR_SOURCE));
    }
    /* RSA realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->rsa), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->rsa), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_RSA_BASE, mr, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(&ss->rsa), 0,
                           qdev_get_gpio_in(intmatrix_dev, ETS_RSA_INTR_SOURCE));
    }
    /* PMS realization */
    {
        sysbus_realize(SYS_BUS_DEVICE(&ss->pms), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->pms), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_SENSITIVE_BASE, mr, 0);
    }

    /* HMAC realization */
    {
        ss->hmac.parent.efuse = ESP_EFUSE(&ss->efuse);
        qdev_realize(DEVICE(&ss->hmac), &ss->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->hmac), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_HMAC_BASE, mr, 0);
    }


    /* Digital Signature realization */
    {
        ss->ds.parent.hmac = ESP_HMAC(&ss->hmac);
        ss->ds.parent.aes = ESP_AES(&ss->aes);
        ss->ds.parent.rsa = ESP_RSA(&ss->rsa);
        ss->ds.parent.sha = ESP_SHA(&ss->sha);
        qdev_realize(DEVICE(&ss->ds), &ss->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->ds), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_DIGITAL_SIGNATURE_BASE, mr, 0);
    }
    /* XTS-AES realization */
    {
        ss->xts_aes.efuse = ESP_EFUSE(&ss->efuse);
        ss->xts_aes.clock = &ss->clock;
        qdev_realize(DEVICE(&ss->xts_aes), &ss->periph_bus, &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->xts_aes), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_AES_XTS_BASE, mr, 0);
    }

    /* RGB display realization */
    {
        /* Give the internal RAM memory region to the display */
        ss->rgb.intram = dram;
        sysbus_realize(SYS_BUS_DEVICE(&ss->rgb), &error_fatal);
        MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&ss->rgb), 0);
        memory_region_add_subregion_overlap(sys_mem, DR_REG_FRAMEBUF_BASE, mr, 0);
        memory_region_add_subregion_overlap(sys_mem, esp32s3_memmap[ESP32S3_MEMREGION_FRAMEBUF].base, &ss->rgb.vram, 0);
    }

    esp32s3_soc_add_unimp_device(sys_mem, "esp32s3.rmt", DR_REG_RMT_BASE, 0x1000);
    esp32s3_soc_add_unimp_device(sys_mem, "esp32s3.iomux", DR_REG_IO_MUX_BASE, 0x2000);

    
    esp32s3_machine_init_sd(ss);

    /* Need MMU initialized prior to ELF loading,
     * so that ELF gets loaded into virtual addresses
     */
    cpu_reset(CPU(&ss->cpu[0]));

    const char *load_elf_filename = NULL;
    if (machine->firmware) {
        load_elf_filename = machine->firmware;
    }
    if (machine->kernel_filename) {
        qemu_log("Warning: both -bios and -kernel arguments specified. Only loading the the -kernel file.\n");
        load_elf_filename = machine->kernel_filename;
    }

    if (load_elf_filename) {
        uint64_t elf_entry;
        uint64_t elf_lowaddr;
        int size = load_elf(load_elf_filename, NULL,
                               translate_phys_addr, &ss->cpu[0],
                               &elf_entry, &elf_lowaddr,
                               NULL, NULL, 0, EM_XTENSA, 0, 0);
        if (size < 0) {
            error_report("Error: could not load ELF file '%s'", load_elf_filename);
            exit(1);
        }

        if (elf_entry != XCHAL_RESET_VECTOR_PADDR) {
            // Since ROM is empty when loading elf file AND
            // PC value is 0x40000400 after reset
            // need to jump to elf entry point to run a programm
            uint8_t p[4];
            memcpy(p, &elf_entry, 4);
            uint8_t boot[] = {
                0x06, 0x01, 0x00,       /* j    1 */
                0x00,                   /* .literal_position */
                p[0], p[1], p[2], p[3], /* .literal elf_entry */
                                        /* 1: */
                0x01, 0xff, 0xff,       /* l32r a0, elf_entry */
                0xa0, 0x00, 0x00,       /* jx   a0 */
            };
            // Write boot function to reset-vector address (0x40000400) of the CPU 0
            rom_add_blob_fixed_as("boot", boot, sizeof(boot), XCHAL_RESET_VECTOR_PADDR, CPU(&ss->cpu[0])->as);
            ss->cpu[0].env.pc = XCHAL_RESET_VECTOR_PADDR;
        }
    } else {
        char *rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32s3_rev0_rom.bin");
        if (rom_binary == NULL) {
            error_report("Error: -bios argument not set, and ROM code binary not found (1)");
            exit(1);
        }

        int size = load_image_targphys_as(rom_binary, esp32s3_memmap[ESP32S3_MEMREGION_IROM].base, esp32s3_memmap[ESP32S3_MEMREGION_IROM].size, CPU(&ss->cpu[0])->as);
        if (size < 0) {
            error_report("Error: could not load ROM binary '%s'", rom_binary);
            exit(1);
        }
        g_free(rom_binary);

        if (ESP32S3_CPU_COUNT > 1)
        {
            rom_binary = qemu_find_file(QEMU_FILE_TYPE_BIOS, "esp32s3_rev0_rom.bin");
            if (rom_binary == NULL) {
                error_report("Error: -bios argument not set, and ROM code binary not found (2)");
                exit(1);
            }

            size = load_image_targphys_as(rom_binary, esp32s3_memmap[ESP32S3_MEMREGION_IROM].base, esp32s3_memmap[ESP32S3_MEMREGION_IROM].size, CPU(&ss->cpu[1])->as);
            if (size < 0) {
                error_report("Error: could not load ROM binary '%s'", rom_binary);
                exit(1);
            }
            g_free(rom_binary);
        }
    }
}


static ram_addr_t esp32s3_fixup_ram_size(ram_addr_t requested_size)
{
    ram_addr_t size;
    if (requested_size == 0) {
        size = 0;
    } else if (requested_size <= 2 * MiB) {
        size = 2 * MiB;
    } else if (requested_size <= 4 * MiB ) {
        size = 4 * MiB;
    } else if (requested_size <= 8 * MiB ) {
        size = 8 * MiB;
    } else if (requested_size <= 16 * MiB ) {
        size = 16 * MiB;
    } else if (requested_size <= 32 * MiB ) {
        size = 32 * MiB;
    } else {
        qemu_log("RAM size larger than 32 MB not supported\n");
        size = 32 * MiB;
    }
    return size;
}

/* Initialize machine type */
static char *esp32s3_get_app_trace(Object *obj, Error **errp)
{
    Esp32s3MachineState *ms = ESP32S3_MACHINE(obj);
    return g_strdup(ms->app_trace_mode);
}

static void esp32s3_set_app_trace(Object *obj, const char *value, Error **errp)
{
    Esp32s3MachineState *ms = ESP32S3_MACHINE(obj);
    g_free(ms->app_trace_mode);
    ms->app_trace_mode = g_strdup(value);
}

static char *esp32s3_get_app_trace_path(Object *obj, Error **errp)
{
    Esp32s3MachineState *ms = ESP32S3_MACHINE(obj);
    return g_strdup(ms->app_trace_path);
}

static void esp32s3_set_app_trace_path(Object *obj, const char *value,
                                      Error **errp)
{
    Esp32s3MachineState *ms = ESP32S3_MACHINE(obj);
    g_free(ms->app_trace_path);
    ms->app_trace_path = g_strdup(value);
}

static void esp32s3_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Espressif ESP32S3 machine";
    mc->init = esp32s3_machine_init;
    mc->max_cpus = 2;
    mc->default_cpus = 2;
    mc->default_ram_size = 0;
    mc->fixup_ram_size = esp32s3_fixup_ram_size;

    object_class_property_add_str(oc, "app-trace",
                                  esp32s3_get_app_trace, esp32s3_set_app_trace);
    object_class_property_set_description(oc, "app-trace",
        "Enable AppTrace JTAG host emulation (mode: file_io) so "
        "esp_gcov_dump() can write coverage data to the host filesystem. "
        "WARNING: without app-trace-path the guest can read/write any host "
        "file with the privileges of the QEMU process");
    object_class_property_add_str(oc, "app-trace-path",
                                  esp32s3_get_app_trace_path,
                                  esp32s3_set_app_trace_path);
    object_class_property_set_description(oc, "app-trace-path",
        "Confine AppTrace file paths under this host directory "
        "(default: none, paths used verbatim)");
}

static const TypeInfo esp32s3_info = {
    .name = TYPE_ESP32S3_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Esp32s3MachineState),
    .class_init = esp32s3_machine_class_init,
};

static void esp32s3_machine_type_init(void)
{
    type_register_static(&esp32s3_info);
}

type_init(esp32s3_machine_type_init);


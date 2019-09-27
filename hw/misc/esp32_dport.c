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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/misc/esp32_reg.h"
#include "hw/misc/esp32_dport.h"
#include "target/xtensa/cpu.h"


#define ESP32_DPORT_SIZE        (DR_REG_DPORT_APB_BASE - DR_REG_DPORT_BASE)

#define MMU_RANGE_SIZE          (ESP32_CACHE_PAGES_PER_REGION * sizeof(uint32_t))
#define MMU_RANGE_LAST          (MMU_RANGE_SIZE - sizeof(uint32_t))

#define PRO_DROM0_MMU_FIRST     (DR_REG_FLASH_MMU_TABLE_PRO - DR_REG_DPORT_BASE)
#define PRO_DROM0_MMU_LAST      (PRO_DROM0_MMU_FIRST + MMU_RANGE_LAST)
#define PRO_IRAM0_MMU_FIRST     (DR_REG_FLASH_MMU_TABLE_PRO - DR_REG_DPORT_BASE + MMU_RANGE_SIZE)
#define PRO_IRAM0_MMU_LAST      (PRO_IRAM0_MMU_FIRST + MMU_RANGE_LAST)
#define APP_DROM0_MMU_FIRST     (DR_REG_FLASH_MMU_TABLE_APP - DR_REG_DPORT_BASE)
#define APP_DROM0_MMU_LAST      (APP_DROM0_MMU_FIRST + MMU_RANGE_LAST)
#define APP_IRAM0_MMU_FIRST     (DR_REG_FLASH_MMU_TABLE_APP - DR_REG_DPORT_BASE + MMU_RANGE_SIZE)
#define APP_IRAM0_MMU_LAST      (APP_IRAM0_MMU_FIRST + MMU_RANGE_LAST)
#define MMU_ENTRY_MASK          0x1ff

static void esp32_cache_state_update(Esp32CacheState* cs);
static void esp32_cache_data_sync(Esp32CacheRegionState* crs);

static inline uint32_t get_mmu_entry(Esp32CacheRegionState* crs, hwaddr base, hwaddr addr)
{
    return crs->mmu_table[(addr - base)/sizeof(uint32_t)] & MMU_ENTRY_MASK;
}

static inline void set_mmu_entry(Esp32CacheRegionState* crs, hwaddr base, hwaddr addr, uint64_t val)
{
    uint32_t old_val = crs->mmu_table[(addr - base)/sizeof(uint32_t)];
    if (val != old_val) {
        crs->mmu_table[(addr - base)/sizeof(uint32_t)] = (val & MMU_ENTRY_MASK) | ESP32_CACHE_MMU_ENTRY_CHANGED;
    }
}

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
    case A_DPORT_CPU_PER_CONF:
        r = s->cpuperiod_sel;
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
    case A_DPORT_PRO_DCACHE_DBUG0:
    case A_DPORT_APP_DCACHE_DBUG0:
        /* in idle state */
        r = FIELD_DP32(0, DPORT_PRO_DCACHE_DBUG0, CACHE_STATE, 1);
        break;
    case PRO_DROM0_MMU_FIRST ... PRO_DROM0_MMU_LAST:
        r = get_mmu_entry(&s->cache_state[0].drom0, PRO_DROM0_MMU_FIRST, addr);
        break;
    case PRO_IRAM0_MMU_FIRST ... PRO_IRAM0_MMU_LAST:
        r = get_mmu_entry(&s->cache_state[0].iram0, PRO_IRAM0_MMU_FIRST, addr);
        break;
    case APP_DROM0_MMU_FIRST ... APP_DROM0_MMU_LAST:
        r = get_mmu_entry(&s->cache_state[1].drom0, APP_DROM0_MMU_FIRST, addr);
        break;
    case APP_IRAM0_MMU_FIRST ... APP_IRAM0_MMU_LAST:
        r = get_mmu_entry(&s->cache_state[1].iram0, APP_IRAM0_MMU_FIRST, addr);
        break;
    }

    return r;
}

static void esp32_dport_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    Esp32DportState *s = ESP32_DPORT(opaque);
    bool old_state;
    uint32_t old_val;
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
    case A_DPORT_CPU_PER_CONF:
        s->cpuperiod_sel = value & R_DPORT_CPU_PER_CONF_CPUPERIOD_SEL_MASK;
        qemu_irq_pulse(s->clk_update_req);
        break;
    case A_DPORT_PRO_CACHE_CTRL:
        if (FIELD_EX32(value, DPORT_PRO_CACHE_CTRL, CACHE_FLUSH_ENA)) {
            value |= R_DPORT_PRO_CACHE_CTRL_CACHE_FLUSH_DONE_MASK;
            value &= ~R_DPORT_PRO_CACHE_CTRL_CACHE_FLUSH_ENA_MASK;
            esp32_cache_data_sync(&s->cache_state[0].drom0);
            esp32_cache_data_sync(&s->cache_state[0].iram0);
        }
        old_val = s->cache_state[0].cache_ctrl_reg;
        s->cache_state[0].cache_ctrl_reg = value;
        if (value != old_val) {
            esp32_cache_state_update(&s->cache_state[0]);
        }
        break;
    case A_DPORT_PRO_CACHE_CTRL1:
        old_val = s->cache_state[0].cache_ctrl1_reg;
        s->cache_state[0].cache_ctrl1_reg = value;
        if (value != old_val) {
            esp32_cache_state_update(&s->cache_state[0]);
        }
        break;
    case A_DPORT_APP_CACHE_CTRL:
        if (FIELD_EX32(value, DPORT_APP_CACHE_CTRL, CACHE_FLUSH_ENA)) {
            value |= R_DPORT_APP_CACHE_CTRL_CACHE_FLUSH_DONE_MASK;
            value &= ~R_DPORT_APP_CACHE_CTRL_CACHE_FLUSH_ENA_MASK;
            esp32_cache_data_sync(&s->cache_state[1].drom0);
            esp32_cache_data_sync(&s->cache_state[1].iram0);
        }
        old_val = s->cache_state[1].cache_ctrl_reg;
        s->cache_state[1].cache_ctrl_reg = value;
        if (value != old_val) {
            esp32_cache_state_update(&s->cache_state[1]);
        }
        break;
    case A_DPORT_APP_CACHE_CTRL1:
        old_val = s->cache_state[1].cache_ctrl1_reg;
        s->cache_state[1].cache_ctrl1_reg = value;
        if (value != old_val) {
            esp32_cache_state_update(&s->cache_state[1]);
        }
        break;
    case PRO_DROM0_MMU_FIRST ... PRO_DROM0_MMU_LAST:
        set_mmu_entry(&s->cache_state[0].drom0, PRO_DROM0_MMU_FIRST, addr, value);
        break;
    case PRO_IRAM0_MMU_FIRST ... PRO_IRAM0_MMU_LAST:
        set_mmu_entry(&s->cache_state[0].iram0, PRO_IRAM0_MMU_FIRST, addr, value);
        break;
    case APP_DROM0_MMU_FIRST ... APP_DROM0_MMU_LAST:
        set_mmu_entry(&s->cache_state[1].drom0, APP_DROM0_MMU_FIRST, addr, value);
        break;
    case APP_IRAM0_MMU_FIRST ... APP_IRAM0_MMU_LAST:
        set_mmu_entry(&s->cache_state[1].iram0, APP_IRAM0_MMU_FIRST, addr, value);
        break;
    }
}

static const MemoryRegionOps esp32_dport_ops = {
    .read =  esp32_dport_read,
    .write = esp32_dport_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_cache_data_sync(Esp32CacheRegionState* crs)
{
    uint8_t* cache_data = (uint8_t*) memory_region_get_ram_ptr(&crs->mem);
    int n = 0;
    for (int i = 0; i < ESP32_CACHE_PAGES_PER_REGION; ++i) {
        uint32_t* cache_page = (uint32_t*) (cache_data + i * ESP32_CACHE_PAGE_SIZE);
        uint32_t mmu_entry = crs->mmu_table[i];
        if (!(mmu_entry & ESP32_CACHE_MMU_ENTRY_CHANGED)) {
            continue;
        }
        mmu_entry &= MMU_ENTRY_MASK;
        if (mmu_entry & ESP32_CACHE_MMU_INVALID_VAL) {
            uint32_t fill_val = crs->type == ESP32_DCACHE ? 0xbaadbaad : 0x00000000;
            for (int word = 0; word < ESP32_CACHE_PAGE_SIZE / sizeof(uint32_t); ++word) {
                cache_page[word] = fill_val;
            }
        } else {
            uint32_t phys_addr = mmu_entry * ESP32_CACHE_PAGE_SIZE;
            blk_pread(crs->cache->dport->flash_blk, phys_addr, cache_page, ESP32_CACHE_PAGE_SIZE);
        }
        crs->mmu_table[i] &= ~ESP32_CACHE_MMU_ENTRY_CHANGED;
        n++;
    }
    memory_region_flush_rom_device(&crs->mem, 0, ESP32_CACHE_REGION_SIZE);
}

static void esp32_cache_state_update(Esp32CacheState* cs)
{
    bool cache_enabled = FIELD_EX32(cs->cache_ctrl_reg, DPORT_PRO_CACHE_CTRL, CACHE_ENA) != 0;

    bool drom0_enabled = cache_enabled &&
        FIELD_EX32(cs->cache_ctrl1_reg, DPORT_PRO_CACHE_CTRL1, MASK_DROM0) == 0;
    if (!cs->drom0.mem.enabled && drom0_enabled) {
        esp32_cache_data_sync(&cs->drom0);
    }
    memory_region_set_enabled(&cs->drom0.mem, drom0_enabled);

    bool iram0_enabled = cache_enabled &&
        FIELD_EX32(cs->cache_ctrl1_reg, DPORT_PRO_CACHE_CTRL1, MASK_IRAM0) == 0;
    if (!cs->iram0.mem.enabled && iram0_enabled) {
        esp32_cache_data_sync(&cs->iram0);
    }
    memory_region_set_enabled(&cs->iram0.mem, iram0_enabled);
}

static void esp32_cache_region_reset(Esp32CacheRegionState *crs)
{
    for (int i = 0; i < ESP32_CACHE_PAGES_PER_REGION; ++i) {
        crs->mmu_table[i] = ESP32_CACHE_MMU_ENTRY_CHANGED;
    }
}

static void esp32_cache_reset(Esp32CacheState *cs)
{
    esp32_cache_region_reset(&cs->drom0);
    esp32_cache_region_reset(&cs->iram0);
}

static const MemoryRegionOps esp32_cache_ops = {
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp32_dport_reset(DeviceState *dev)
{
    Esp32DportState *s = ESP32_DPORT(dev);

    s->appcpu_boot_addr = 0;
    s->appcpu_clkgate_state = false;
    s->appcpu_reset_state = true;
    s->appcpu_stall_state = false;
    esp32_cache_reset(&s->cache_state[0]);
    esp32_cache_reset(&s->cache_state[1]);
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

    int n_crosscore_irqs = ESP32_DPORT_CROSSCORE_INT_COUNT;
    object_property_set_int(OBJECT(&s->crosscore_int), n_crosscore_irqs, "n_irqs", &error_abort);
    object_property_set_bool(OBJECT(&s->crosscore_int), true, "realized", &error_abort);
    memory_region_add_subregion_overlap(&s->iomem, ESP32_DPORT_CROSSCORE_INT_BASE, &s->crosscore_int.iomem, -1);

    for (int index = 0; index < n_crosscore_irqs; ++index) {
        qemu_irq target = qdev_get_gpio_in(DEVICE(&s->intmatrix), ETS_FROM_CPU_INTR0_SOURCE + index);
        assert(target);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->crosscore_int), index, target);
    }
}

static void esp32_cache_init_region(Esp32CacheState *cs,
                                    Esp32CacheRegionState *crs,
                                    Esp32CacheRegionType type,
                                    uint32_t enable_mask,
                                    const char* name, hwaddr base)
{
    char desc[16];
    crs->cache = cs;
    crs->type = type;
    crs->base = base;
    snprintf(desc, sizeof(desc), "cpu%d-%s", cs->core_id, name);
    memory_region_init_rom_device(&crs->mem, OBJECT(cs->dport),
                                  &esp32_cache_ops, crs,
                                  desc, ESP32_CACHE_REGION_SIZE, &error_abort);
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
    /* memory_region_add_subregion_overlap is called from esp32_dport_realize, since crosscore_int doesn't know its iomem size yet */

    for (int i = 0; i < ESP32_CPU_COUNT; ++i) {
        Esp32CacheState* cs = &s->cache_state[i];
        cs->core_id = i;
        cs->dport = s;
        esp32_cache_init_region(cs, &cs->drom0, ESP32_DCACHE,
                                R_DPORT_PRO_CACHE_CTRL1_MASK_DROM0_MASK, "drom0", 0x3F400000);
        esp32_cache_init_region(cs, &cs->iram0, ESP32_ICACHE,
                                R_DPORT_PRO_CACHE_CTRL1_MASK_IRAM0_MASK, "iram0", 0x40000000);
    }

    qdev_init_gpio_out_named(DEVICE(sbd), &s->appcpu_stall_req, ESP32_DPORT_APPCPU_STALL_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->appcpu_reset_req, ESP32_DPORT_APPCPU_RESET_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->clk_update_req, ESP32_DPORT_CLK_UPDATE_GPIO, 1);
}

static Property esp32_dport_properties[] = {
    DEFINE_PROP_DRIVE("flash", Esp32DportState, flash_blk),
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

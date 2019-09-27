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
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/misc/esp32_reg.h"
#include "hw/misc/esp32_dport.h"

#include "hw/misc/esp32_flash_enc.h"
#include "hw/nvram/esp32_efuse.h"


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
static void esp32_cache_invalidate_all_entries(Esp32CacheRegionState* crs);

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
    case A_DPORT_CACHE_IA_INT_EN:
        r = s->cache_ill_trap_en_reg;
        break;
    case A_DPORT_PRO_DCACHE_DBUG3:
        r = 0;
        r = FIELD_DP32(r, DPORT_PRO_DCACHE_DBUG3, IA_INT_DROM0, s->cache_state[0].drom0.illegal_access_status);
        r = FIELD_DP32(r, DPORT_PRO_DCACHE_DBUG3, IA_INT_IRAM0, s->cache_state[0].iram0.illegal_access_status);
        break;
    case A_DPORT_APP_DCACHE_DBUG3:
        r = 0;
        r = FIELD_DP32(r, DPORT_APP_DCACHE_DBUG3, IA_INT_DROM0, s->cache_state[1].drom0.illegal_access_status);
        r = FIELD_DP32(r, DPORT_APP_DCACHE_DBUG3, IA_INT_IRAM0, s->cache_state[1].iram0.illegal_access_status);
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
    case A_DPORT_SLAVE_SPI_CONFIG:
        r = s->slave_spi_config_reg;
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
            esp32_cache_invalidate_all_entries(&s->cache_state[0].drom0);
            esp32_cache_data_sync(&s->cache_state[0].drom0);
            esp32_cache_invalidate_all_entries(&s->cache_state[0].iram0);
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
            esp32_cache_invalidate_all_entries(&s->cache_state[1].drom0);
            esp32_cache_data_sync(&s->cache_state[1].drom0);
            esp32_cache_invalidate_all_entries(&s->cache_state[1].iram0);
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
    case A_DPORT_CACHE_IA_INT_EN:
        s->cache_ill_trap_en_reg = value;
        s->cache_state[0].drom0.illegal_access_trap_en = (FIELD_EX32(value, DPORT_CACHE_IA_INT_EN, IA_INT_PRO_DROM0));
        s->cache_state[0].iram0.illegal_access_trap_en = (FIELD_EX32(value, DPORT_CACHE_IA_INT_EN, IA_INT_PRO_IRAM0));
        s->cache_state[1].drom0.illegal_access_trap_en = (FIELD_EX32(value, DPORT_CACHE_IA_INT_EN, IA_INT_APP_DROM0));
        s->cache_state[1].iram0.illegal_access_trap_en = (FIELD_EX32(value, DPORT_CACHE_IA_INT_EN, IA_INT_APP_IRAM0));
        s->cache_state[0].dram1.illegal_access_trap_en = (FIELD_EX32(value, DPORT_CACHE_IA_INT_EN, IA_INT_PRO_DRAM1));
        s->cache_state[1].dram1.illegal_access_trap_en = (FIELD_EX32(value, DPORT_CACHE_IA_INT_EN, IA_INT_APP_DRAM1));
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
    case A_DPORT_SLAVE_SPI_CONFIG:
        s->slave_spi_config_reg = value;
        qemu_set_irq(s->flash_enc_en_gpio, FIELD_EX32(value, DPORT_SLAVE_SPI_CONFIG, SLAVE_SPI_ENCRYPT_ENABLE));
        qemu_set_irq(s->flash_dec_en_gpio, FIELD_EX32(value, DPORT_SLAVE_SPI_CONFIG, SLAVE_SPI_DECRYPT_ENABLE));
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
    if (crs->cache->dport->flash_blk == NULL) {
        return;
    }

    Esp32FlashEncryptionState * flash_enc = esp32_flash_encryption_find();
    bool decrypt = (flash_enc != NULL && esp32_flash_decryption_enabled(flash_enc));

    uint8_t* cache_data = (uint8_t*) memory_region_get_ram_ptr(&crs->mem);
    for (int i = 0; i < ESP32_CACHE_PAGES_PER_REGION; ++i) {
        uint32_t* cache_page = (uint32_t*) (cache_data + i * ESP32_CACHE_PAGE_SIZE);
        uint32_t mmu_entry = crs->mmu_table[i];
        if (!(mmu_entry & ESP32_CACHE_MMU_ENTRY_CHANGED)) {
            continue;
        }
        mmu_entry &= MMU_ENTRY_MASK;
        if (mmu_entry & ESP32_CACHE_MMU_INVALID_VAL) {
            uint32_t fill_val = crs->illegal_access_retval;
            for (int word = 0; word < ESP32_CACHE_PAGE_SIZE / sizeof(uint32_t); ++word) {
                cache_page[word] = fill_val;
            }
        } else {
            uint32_t phys_addr = mmu_entry * ESP32_CACHE_PAGE_SIZE;
            blk_pread(crs->cache->dport->flash_blk, phys_addr, ESP32_CACHE_PAGE_SIZE, cache_page, 0);
            if (decrypt) {
                esp32_flash_decrypt_inplace(flash_enc, phys_addr, cache_page, ESP32_CACHE_PAGE_SIZE/4);
            }
        }
        crs->mmu_table[i] &= ~ESP32_CACHE_MMU_ENTRY_CHANGED;
    }
    memory_region_flush_rom_device(&crs->mem, 0, ESP32_CACHE_REGION_SIZE);
}

static void esp32_cache_invalidate_all_entries(Esp32CacheRegionState* crs)
{
    for (int i = 0; i < ESP32_CACHE_PAGES_PER_REGION; ++i) {
        crs->mmu_table[i] |= ESP32_CACHE_MMU_ENTRY_CHANGED;
    }
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

    if (cs->dport->has_psram) {
        bool dram1_enabled = cache_enabled &&
            FIELD_EX32(cs->cache_ctrl1_reg, DPORT_PRO_CACHE_CTRL1, MASK_DRAM1) == 0;
        memory_region_set_enabled(&cs->dram1.mem, dram1_enabled);
    }
}

static void esp32_cache_region_reset(Esp32CacheRegionState *crs)
{
    for (int i = 0; i < ESP32_CACHE_PAGES_PER_REGION; ++i) {
        crs->mmu_table[i] = ESP32_CACHE_MMU_ENTRY_CHANGED;
    }
    crs->illegal_access_trap_en = false;
}

static void esp32_cache_reset(Esp32CacheState *cs)
{
    esp32_cache_region_reset(&cs->drom0);
    esp32_cache_region_reset(&cs->iram0);
}

static uint64_t esp32_cache_ill_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32CacheRegionState *crs = (Esp32CacheRegionState*) opaque;
    uint32_t ill_data[] = { crs->illegal_access_retval, crs->illegal_access_retval };
    uint32_t result;
    memcpy(&result, ((uint8_t*) ill_data) + (addr % 4), size);
    if (crs->illegal_access_trap_en) {
        crs->illegal_access_status = true;
        qemu_irq_raise(crs->cache->dport->cache_ill_irq);
    }
    return result;
}

static void esp32_cache_ill_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned int size)
{
}

static bool esp32_cache_ill_accepts(void *opaque, hwaddr addr,
                             unsigned size, bool is_write,
                             MemTxAttrs attrs)
{
    return !is_write;
}


void esp32_dport_clear_ill_trap_state(Esp32DportState* s)
{
    s->cache_state[0].drom0.illegal_access_status = false;
    s->cache_state[1].drom0.illegal_access_status = false;
    s->cache_state[0].iram0.illegal_access_status = false;
    s->cache_state[1].iram0.illegal_access_status = false;
    s->cache_state[0].dram1.illegal_access_status = false;
    s->cache_state[1].dram1.illegal_access_status = false;
    qemu_irq_lower(s->cache_ill_irq);
}

static const MemoryRegionOps esp32_cache_ops = {
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.accepts = esp32_cache_ill_accepts,
};


static const MemoryRegionOps esp32_cache_ill_trap_ops = {
    .read = esp32_cache_ill_read,
    .write = esp32_cache_ill_write,
};

static void esp32_dport_reset(DeviceState *dev)
{
    Esp32DportState *s = ESP32_DPORT(dev);

    s->appcpu_boot_addr = 0;
    s->appcpu_clkgate_state = false;
    s->appcpu_reset_state = true;
    s->appcpu_stall_state = false;
    s->cache_ill_trap_en_reg = 0;
    esp32_cache_reset(&s->cache_state[0]);
    esp32_cache_reset(&s->cache_state[1]);
    qemu_irq_lower(s->appcpu_stall_req);
}

static void esp32_dport_realize(DeviceState *dev, Error **errp)
{
    Esp32DportState *s = ESP32_DPORT(dev);
    MachineState *ms = MACHINE(qdev_get_machine());

    s->cpu_count = ms->smp.cpus;
}

static void esp32_cache_init_region(Esp32CacheState *cs,
                                    Esp32CacheRegionState *crs,
                                    Esp32CacheRegionType type,
                                    const char* name, hwaddr base,
                                    uint32_t illegal_access_retval)
{
    char desc[16];
    crs->cache = cs;
    crs->type = type;
    crs->base = base;
    crs->illegal_access_retval = illegal_access_retval;
    snprintf(desc, sizeof(desc), "cpu%d-%s", cs->core_id, name);
    if (type == ESP32_DCACHE_PSRAM) {
        memory_region_init_ram(&crs->mem, OBJECT(cs->dport), desc,
                               ESP32_CACHE_REGION_SIZE, &error_abort);
    } else {
        memory_region_init_rom_device(&crs->mem, OBJECT(cs->dport),
                                    &esp32_cache_ops, crs,
                                    desc, ESP32_CACHE_REGION_SIZE, &error_abort);
    }

    snprintf(desc, sizeof(desc), "cpu%d-%s-ill", cs->core_id, name);
    memory_region_init_io(&crs->illegal_access_trap_mem, OBJECT(cs->dport),
                          &esp32_cache_ill_trap_ops, crs,
                          desc, ESP32_CACHE_REGION_SIZE);
}

static void esp32_dport_init(Object *obj)
{
    Esp32DportState *s = ESP32_DPORT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_dport_ops, s,
                          TYPE_ESP32_DPORT, ESP32_DPORT_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    for (int i = 0; i < ESP32_CPU_COUNT; ++i) {
        Esp32CacheState* cs = &s->cache_state[i];
        cs->core_id = i;
        cs->dport = s;
        esp32_cache_init_region(cs, &cs->drom0, ESP32_DCACHE_FLASH, "drom0",
                                0x3F400000, 0xbaadbaad);
        esp32_cache_init_region(cs, &cs->iram0, ESP32_ICACHE_FLASH, "iram0",
                                0x40000000, 0x00000000);
        esp32_cache_init_region(cs, &cs->dram1, ESP32_DCACHE_PSRAM, "dram1",
                                0x3F800000, 0xbaadbaad);
    }

    qdev_init_gpio_out_named(DEVICE(sbd), &s->appcpu_stall_req, ESP32_DPORT_APPCPU_STALL_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->appcpu_reset_req, ESP32_DPORT_APPCPU_RESET_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->clk_update_req, ESP32_DPORT_CLK_UPDATE_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->cache_ill_irq, ESP32_DPORT_CACHE_ILL_IRQ_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->flash_enc_en_gpio, ESP32_DPORT_FLASH_ENC_EN_GPIO, 1);
    qdev_init_gpio_out_named(DEVICE(sbd), &s->flash_dec_en_gpio, ESP32_DPORT_FLASH_DEC_EN_GPIO, 1);
}

static Property esp32_dport_properties[] = {
    DEFINE_PROP_DRIVE("flash", Esp32DportState, flash_blk),
    DEFINE_PROP_BOOL("has_psram", Esp32DportState, has_psram, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_dport_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_dport_reset;
    dc->realize = esp32_dport_realize;
    device_class_set_props(dc, esp32_dport_properties);
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

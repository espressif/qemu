/* Copyright 2019 Espressif Systems (Shanghai) Co. Ltd.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/boot.h"
#include "hw/char/esp32_uart.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#define TYPE_ESP_RVTEST "esp_rvtest"
#define TYPE_RISCV_CPU_ESP_RVTEST  RISCV_CPU_TYPE_NAME("esp-rvtest")

#define ESP_RVTEST(obj) \
    OBJECT_CHECK(EspRVTestState, (obj), TYPE_ESP_RVTEST)

typedef struct EspRVTestState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVCPU cpu;
    ESP32UARTState uart;
} EspRVTestState;

enum {
    ESP_RVTEST_DRAM,
    ESP_RVTEST_IRAM,
    ESP_RVTEST_UART
};

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} esp_rvtest_memmap[] = {
    [ESP_RVTEST_DRAM] = { 0x3FFB0000, 0x04000 },
    [ESP_RVTEST_IRAM] = { 0x00010000, 0x40000 },
    [ESP_RVTEST_UART] = { 0x3ff40000, 0x1000  },
};

static void riscv_esp_rvtest_cpu_init(Object *obj)
{
    RISCVCPU* cpu = RISCV_CPU(obj);
    cpu->cfg.mmu = false;
    cpu->cfg.pmp = false;
    CPURISCVState *env = &cpu->env;
    env->misa = RV32 | RVI | RVM | RVC | RVD | RVF;
    env->priv_ver = PRIV_VERSION_1_11_0;
    env->resetvec = esp_rvtest_memmap[ESP_RVTEST_IRAM].base;
    env->features = 0;
}

static void riscv_esp_rvtest_init(Object *obj)
{
    EspRVTestState *s = ESP_RVTEST(obj);

    object_initialize_child(obj, "cpu", &s->cpu, sizeof(s->cpu),
                            TYPE_RISCV_CPU_ESP_RVTEST, &error_abort, NULL);

    object_initialize_child(obj, "uart", &s->uart, sizeof(s->uart),
                               TYPE_ESP32_UART, &error_abort, NULL);

    object_property_add_alias(obj, "serial0", OBJECT(&s->uart), "chardev",
                                  &error_abort);

}

static void riscv_esp_rvtest_realize(DeviceState *dev, Error **errp)
{
    EspRVTestState *s = ESP_RVTEST(dev);

    const struct MemmapEntry *memmap = esp_rvtest_memmap;
    MemoryRegion *sys_mem = get_system_memory();

    MemoryRegion *dram = g_new(MemoryRegion, 1);
    MemoryRegion *iram = g_new(MemoryRegion, 1);

    memory_region_init_ram(dram, NULL, "esp_rvtest.dram",
                           memmap[ESP_RVTEST_DRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP_RVTEST_DRAM].base, dram);

    memory_region_init_ram(iram, NULL, "esp_rvtest.iram",
                           memmap[ESP_RVTEST_IRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[ESP_RVTEST_IRAM].base, iram);

    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &error_abort);

    object_property_set_bool(OBJECT(&s->uart), true, "realized", &error_abort);

    MemoryRegion *uart = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0);
    memory_region_add_subregion_overlap(sys_mem, memmap[ESP_RVTEST_UART].base, uart, 0);
}

/* Initialize machine instance */
static void riscv_esp_rvtest_machine_inst_init(MachineState *machine)
{
    EspRVTestState *s = g_new0(EspRVTestState, 1);

    object_initialize_child(OBJECT(machine), "soc", s, sizeof(*s),
                            TYPE_ESP_RVTEST, &error_abort, NULL);

    qdev_prop_set_chr(DEVICE(s), "serial0", serial_hd(0));

    object_property_set_bool(OBJECT(s), true, "realized", &error_abort);

    if (machine->kernel_filename) {
        riscv_load_kernel(machine->kernel_filename);
    }
}

/* Initialize machine type */
static void riscv_esp_rvtest_machine_init(MachineClass *mc)
{
    mc->desc = "Espressif RISC-V test platform";
    mc->init = riscv_esp_rvtest_machine_inst_init;
}

DEFINE_MACHINE("esp_rvtest", riscv_esp_rvtest_machine_init)

static void riscv_esp_rvtest_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = riscv_esp_rvtest_realize;
}

static const TypeInfo riscv_esp_rvtest_type_infos[] = {
    {
        .name = TYPE_ESP_RVTEST,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(EspRVTestState),
        .instance_init = riscv_esp_rvtest_init,
        .class_init = riscv_esp_rvtest_soc_class_init,
    },
    {
        .name = TYPE_RISCV_CPU_ESP_RVTEST,
        .parent = TYPE_RISCV_CPU,
        .instance_init = riscv_esp_rvtest_cpu_init
    }
};

DEFINE_TYPES(riscv_esp_rvtest_type_infos)

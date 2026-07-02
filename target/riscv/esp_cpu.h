/*
 * Espressif RISC-V CPU
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/registerfields.h"
#include "cpu.h"

/* Make sure we are not in CONFIG_USER_ONLY */
#if defined(CONFIG_USER_ONLY) || !defined(CONFIG_SOFTMMU)
#error "ESP RISC-V Core only works in system emulation and in SOFTMMU configuration"
#endif

#include "hw/core/tcg-cpu-ops.h"

#define ESP_CPU_IRQ_LINES_NAME "espressif-cpu-irq-lines"

#define TYPE_ESP_RISCV_CPU      "espressif-riscv-cpu"
#define ESP_CPU(obj)            OBJECT_CHECK(EspRISCVCPU, (obj), TYPE_ESP_RISCV_CPU)
#define ESP_CPU_GET_CLASS(obj)  OBJECT_GET_CLASS(EspRISCVCPUClass, obj, TYPE_ESP_RISCV_CPU)
#define ESP_CPU_CLASS(klass)    OBJECT_CLASS_CHECK(EspRISCVCPUClass, klass, TYPE_ESP_RISCV_CPU)

#define ESP_CPU_INT_LINES   31


/* Define a type that will be used to generate a cycle counter */
typedef struct {
    uint64_t former_time;
    uint64_t former_rem_cycles;
    uint64_t cycles;
    /* The number of picoseconds an instruction takes to execute */
    uint64_t divider;
} ESPCPUCycleCounter;

/**
 * Espressif's RISC-V core is different from standard RISC-V because of the way interrupts are handled.
 * Extend the standard RISC-V core implementation.
 */
typedef struct EspRISCVCPU {
    /*< private >*/
    RISCVCPU parent_obj;

    /* Cycle counts */
    ESPCPUCycleCounter cc_user;
    ESPCPUCycleCounter cc_machine;

    /*< public >*/
    /* The parent object already has a reset vector property */
    uint32_t hartid_base;
    /* Parent IRQ_M line */
    qemu_irq parent_irq;
    /* Bitmap of interrupt matrix output lines currently asserted on CPU inputs. */
    uint32_t irq_lines;
    /* Espressif PMA (Physical Memory Attribute) extension. Set by SOC machine
     * code: the ESP32-C6 needs the PMA CSRs registered, the ESP32-C3 does not. */
    bool has_pma;
    /* ESP32-C6 (and similar) repurpose the standard RISC-V mie CSR (0x304) as
     * a per-line external-interrupt enable bitmap (MXIE), with the four CLINT
     * enables (USIE, MSIE, UTIE, MTIE) at their classic positions and the
     * remaining 28 bits acting as enables for external interrupts 1..2, 5..6,
     * 8..31 (TRM Reg 1.8, §1.6.2). When this property is set we override the
     * mie CSR ops so writes flow into `mie_enabled` and the intmatrix can use
     * it as a second per-line gate alongside PLIC_MXINT_ENABLE_REG. The
     * ESP32-C3 does not repurpose mie and leaves this disabled. */
    bool mie_as_bitmap;
    /* Per-line external-interrupt enable bitmap, populated by guest writes to
     * the mie CSR when mie_as_bitmap is true. Only meaningful in that mode. */
    uint32_t mie_enabled;
    /* Optional notifier invoked after every guest write to the mie CSR (only
     * when mie_as_bitmap is true). The intmatrix registers itself here so it
     * can refresh the per-line IRQ assertion state. */
    void (*mie_changed_cb)(void *opaque);
    void *mie_changed_opaque;
} EspRISCVCPU;

/**
 * Register a callback to be invoked after every guest write to the mie CSR.
 * Only meaningful when mie_as_bitmap is true on this CPU. The callback is
 * called with `cpu->mie_enabled` already updated.
 */
void esp_cpu_set_mie_changed_cb(EspRISCVCPU *cpu,
                                void (*cb)(void *opaque),
                                void *opaque);


typedef struct EspRISCVCPUClass {
    /*< private >*/
    RISCVCPUClass parent_class;
    DeviceRealize parent_realize;
    DeviceReset parent_reset;
    bool (*parent_exec_interrupt)(CPUState *cpu, int interrupt_request);
    bool (*parent_has_work)(CPUState *cpu);

} EspRISCVCPUClass;

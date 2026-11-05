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

/*
 * mstatus WARL masks for Espressif RISC-V cores with restricted mstatus.
 * Only the bits listed here are writable by the guest; all others (including
 * MPRV, SUM, MXR, SIE, SPIE, SPP, TVM, TSR) are hardwired 0 on silicon.
 */

/* ESP32-C3: MIE(3), MPIE(7), MPP(12:11), TW(21) */
#define ESP32C3_MSTATUS_WARL_MASK  (MSTATUS_MIE | MSTATUS_MPIE | \
                                    MSTATUS_MPP | MSTATUS_TW)

/* ESP32-C6 / ESP32-H2: UIE(0), MIE(3), UPIE(4), MPIE(7), MPP(12:11), TW(21) */
#define ESP32C6_MSTATUS_WARL_MASK  (MSTATUS_MIE | MSTATUS_MPIE | \
                                    MSTATUS_MPP | MSTATUS_TW | \
                                    (1UL << 0) | (1UL << 4))

#define TYPE_ESP_RISCV_CPU      "espressif-riscv-cpu"
#define ESP_CPU(obj)            OBJECT_CHECK(EspRISCVCPU, (obj), TYPE_ESP_RISCV_CPU)
#define ESP_CPU_GET_CLASS(obj)  OBJECT_GET_CLASS(EspRISCVCPUClass, obj, TYPE_ESP_RISCV_CPU)
#define ESP_CPU_CLASS(klass)    OBJECT_CLASS_CHECK(EspRISCVCPUClass, klass, TYPE_ESP_RISCV_CPU)

#define ESP_CPU_INT_LINES   31

/*
 * Highest CPU interrupt line the core can accept. CLIC-based SoCs place the
 * external interrupts above the 16 core-local CLIC lines, so an IDF interrupt
 * number N becomes CLIC line N + 16; the ESP32-P4 therefore drives lines up to
 * 47. PLIC-based SoCs only ever drive lines 1..ESP_CPU_INT_LINES.
 */
#define ESP_CPU_INT_LINES_MAX   47


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
    uint64_t irq_lines;
    /* Espressif PMA (Physical Memory Attribute) extension. Set by SOC machine
     * code: the ESP32-C6 needs the PMA CSRs registered, the ESP32-C3 does not. */
    bool has_pma;
    /* Backing storage for the Espressif PMA CSRs.  PMA enforcement is not
     * modelled, but the values must round-trip through write/read so guest
     * code that configures or probes them observes consistent state. */
    uint32_t pmacfg[16];   /* pmacfg0..15  at 0xBC0..0xBCF */
    uint32_t pmaaddr[16];  /* pmaaddr0..15 at 0xBD0..0xBDF */
    /* Some ESP cores implement 32 PMP entries while the generic RISC-V target
     * only models MAX_RISCV_PMPS (16).  Set by SOC machine code to register
     * the upper half of the PMP CSR space. */
    bool has_pmp32;
    /* Backing storage for PMP entries 16..31.  Like PMA above, the values only
     * round-trip; enforcement is left to the 16 entries the generic target
     * models. */
    uint32_t pmpcfg_hi[4];    /* pmpcfg4..7    at 0x3A4..0x3A7 */
    uint32_t pmpaddr_hi[16];  /* pmpaddr16..31 at 0x3C0..0x3CF */
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
    /* On a CLIC core the per-line external-interrupt enable lives in the CLIC's
     * clicintie[] bytes, which the intmatrix already honours, and mie has no
     * MEIE bit at all. The ESP32-P4 mask ROM clears mie before handing control
     * over, so without this the parent RISC-V dispatcher's `mie & mip` check on
     * IRQ_M_EXT rejects every interrupt the intmatrix raises.
     * Also selects CLIC arbitration: lines are taken by level against
     * mintstatus.mil and mintthresh, and mret restores mil from mcause.mpil. */
    bool clic_ext_irq;
    /* clicintattr and clicintctl bytes of each line, reported by the intmatrix
     * through esp_cpu_clic_set_line(). */
    uint8_t clic_line_attr[ESP_CPU_INT_LINES_MAX + 1];
    uint8_t clic_line_ctl[ESP_CPU_INT_LINES_MAX + 1];

    /* CLIC CSRs (CLIC-based ESP CPUs: ESP32-C5, ESP32-P4).  Same rationale
     * as the PMA storage above -- not modelled, but values must round-trip.
     * Exceptions: esp_cpu_exec_interrupt consumes `mtvt` for CLIC vectoring,
     * and on CLIC cores `mintthresh` and `mintstatus` gate interrupts. */
    target_ulong utvt;            /* 0x007 utvt    */
    target_ulong mtvt;            /* 0x307 mtvt    */
    target_ulong mnxti;           /* 0x345 mnxti   */
    target_ulong mintstatus_alt;  /* 0x346 mintstatus (alt mapping) */
    target_ulong mintthresh;      /* 0x347 mintthresh */
    target_ulong uintstatus;      /* 0xCB1 uintstatus */
    target_ulong mintstatus;      /* 0xFB1 mintstatus */

    /* Machine Hardware Configuration CSR range (0x7C0..0x7CF) used by the
     * ESP32-C5 (mhcr/mhint/etc.).  Indexed by `csrno - 0x7C0`. */
    uint32_t mhcr[16];

    /* mexstatus (0x7E1) on the ESP32-C5.  Same address as PCMR_M on the
     * older C3/C6/H2 cores, where it is a no-op stub; storing it here is
     * harmless on those SOCs and lets the C5 round-trip it. */
    uint32_t mexstatus;

    /* Run-stalled or held in reset by the SoC. Interrupts do not wake the core. */
    bool stalled;

    /* Espressif DSP extension CSRs (ESP32-H4 / P4).  ISR prologues probe
     * CSR_DSP_STATE_REG (0x7f3); without a stub that access illegal-traps
     * and the guest hangs in the panic loop.  State 0 == Off is enough for
     * the probe; the remaining CSRs only need to round-trip. */
    uint32_t dsp_state;    /* 0x7f3 */
    uint32_t dsp_xacc_l;   /* 0x806 */
    uint32_t dsp_xacc_h;   /* 0x807 */
    uint32_t dsp_sar;      /* 0x809 */
    uint32_t dsp_status;   /* 0x80a */
} EspRISCVCPU;

/**
 * @brief Set the rate of the CPU cycle counters, for SOCs that model CPU frequency changes.
 */
void esp_cpu_set_freq(EspRISCVCPU *cpu, uint32_t freq_hz);

/**
 * @brief Report a line's clicintattr and clicintctl bytes to a CLIC core, which needs
 * them to select the line's handler and compute its interrupt level.
 */
void esp_cpu_clic_set_line(EspRISCVCPU *cpu, uint32_t line, uint8_t attr, uint8_t ctl);

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

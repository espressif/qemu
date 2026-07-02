/*
 * Espressif RISC-V CPU
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "sysemu/reset.h"
#include "esp_cpu.h"

#define BIT_SET(reg, bit)   ((reg) & BIT(bit))
#define CLEAR_BIT(reg, bit) do { (reg) &= ~BIT(bit); } while(0)
#define SET_BIT(reg, bit)   do { (reg) |= BIT(bit); } while(0)

/* CSR-related */
#define ESP_CPU_CSR_PCER_M      0x7E0
#define ESP_CPU_CSR_PCMR_M      0x7E1
#define ESP_CPU_CSR_MCYCLE_M    0x7E2

/* The RISC-V core in QEMU doesn't support the triggers used in ESP32-C3
 * tcontrol is not supported either. So let's override all the debug registers
 */
#define ESP_CPU_CSR_TSELECT     0x7A0
#define ESP_CPU_CSR_TDATA1      0x7A1
#define ESP_CPU_CSR_TDATA2      0x7A2
#define ESP_CPU_CSR_TDATA3      0x7A3
#define ESP_CPU_CSR_TCONTROL    0x7A5


#define ESP_CPU_CSR_PCER_U      0x800
#define ESP_CPU_CSR_PCMR_U      0x801
#define ESP_CPU_CSR_MCYCLE_U    0x802

#define ESP_CPU_CSR_GPIO_OEN    0x803
#define ESP_CPU_CSR_GPIO_IN     0x804
#define ESP_CPU_CSR_GPIO_OUT    0x805


static RISCVException esp_cpu_csr_predicate(CPURISCVState *env, int csrno) {
    return RISCV_EXCP_NONE;
}


static uint64_t esp_cpu_get_cycles(ESPCPUCycleCounter* cc)
{
    /* Let's simulate the cycle count between two reads of MCYCLE thanks to the time API. */
    /* Calculate the time elapsed between now and the previous call */
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t diff = 0;

    /* If we are not in the first call, calculate the difference */
    if (cc->former_time != 0) {
        /* The divider is in picoseconds, for a more precise result. It would be possible to simpyl return
         * cc->cycles = (now * 1000 / cc->divider). However, doing so would prevent a future implementation
         * of CPU frequency change as the cycles count would go backward as soon as the frequency is higher
         */
        assert(cc->divider != 0);
        const uint64_t num = (now - cc->former_time) * 1000 + cc->former_rem_cycles;
        diff = num / cc->divider;
        /* Store the remaining executed clock cycles that were not taken into account in the division,
         * they will be added back in the next run */
        cc->former_rem_cycles = num % cc->divider;
    }
    cc->former_time = now;
    cc->cycles += diff;
    return cc->cycles;
}


/**
 * Convert the given environment to the an ESP CPU.
 * The environment is the field part of RISCVCPU, so retrieve the RISCVCPU address.
 * In fact, RISCVCPU is overriden as EspRISCVCPU, we can then cast it safely.
 */
static EspRISCVCPU* esp_cpu_riscv_to_cpu(CPURISCVState *env)
{
    // RISCVCPU* riscv = (RISCVCPU*) ((void*) env - offsetof(RISCVCPU, env));
    RISCVCPU* riscv = container_of(env, RISCVCPU, env);
    return ESP_CPU(riscv);
}


static RISCVException esp_cpu_csr_read(CPURISCVState *env, int csrno, target_ulong *ret_value) {
    EspRISCVCPU *s = esp_cpu_riscv_to_cpu(env);

    if (csrno == ESP_CPU_CSR_MCYCLE_U) {
        *ret_value = esp_cpu_get_cycles(&s->cc_user);
    } else if (csrno == ESP_CPU_CSR_MCYCLE_M) {
        *ret_value = esp_cpu_get_cycles(&s->cc_machine);
    } else if (csrno >= ESP_CPU_CSR_TSELECT && csrno <= ESP_CPU_CSR_TCONTROL) {
        /* Nothing special to do here */
    } else {
        *ret_value = 0;
    }
    return RISCV_EXCP_NONE;
}


static RISCVException esp_cpu_csr_write(CPURISCVState *env, int csrno, target_ulong new_value) {
    EspRISCVCPU *s = esp_cpu_riscv_to_cpu(env);

    if (csrno == ESP_CPU_CSR_MCYCLE_U) {
        s->cc_user.cycles = new_value;
    } else if (csrno == ESP_CPU_CSR_MCYCLE_M) {
        s->cc_machine.cycles = new_value;
    } else if (csrno >= ESP_CPU_CSR_TSELECT && csrno <= ESP_CPU_CSR_TCONTROL) {
        /* Nothing special to do here */
    }

    return RISCV_EXCP_NONE;
}


riscv_csr_operations esp_cpu_csr_ops = {
    .predicate = esp_cpu_csr_predicate,
    .read = esp_cpu_csr_read,
    .write = esp_cpu_csr_write
};


/**
 * Custom mie CSR operations for SOCs (ESP32-C6 and successors) that repurpose
 * the standard RISC-V mie CSR (0x304) as a per-line external-interrupt enable
 * bitmap (MXIE).  Per the ESP32-C6 TRM §1.5.2 (Reg 1.8) and §1.6.2:
 *
 *   - The four CLINT enables stay at their classic positions: USIE bit 0,
 *     MSIE bit 3, UTIE bit 4, MTIE bit 7.
 *   - Bits 1:2, 5:6, 8:31 are MXIE[N] — per-line enables for the 28 external
 *     CPU interrupts.  There is NO standard MEIE; bit 11 is just MXIE[11].
 *   - An external interrupt fires only when *both* the matching MXIE bit in
 *     mie AND the bit in PLIC_MXINT_ENABLE_REG are set ("further needs to be
 *     unmasked at core level by setting the corresponding bit in mie CSR").
 *
 * This override stores writes into `cpu->mie_enabled` (the model's view of
 * the silicon mie register) and notifies the intmatrix so it can refresh per
 * line gating.  We *also* mirror the four standard CLINT bits into the
 * underlying `env->mie` (and force bit 11 / MEIE on) so the parent QEMU
 * RISC-V dispatcher — which still interprets `env->mie` with the architected
 * layout — has consistent state for CLINT interrupts and accepts external
 * IRQs that our intmatrix has already gated via `mie_enabled`.  The MXIE
 * bits are deliberately kept out of `env->mie` because their meaning
 * diverges from the RISC-V standard at those positions (e.g. bit 1 is
 * MXIE[1] on the C6 but SSIE in the standard, bit 11 is MXIE[11] but MEIE
 * in the standard, etc.).
 */
#define ESP_CPU_MIE_CLINT_MASK \
    (BIT(0) /* USIE */ | BIT(3) /* MSIE */ | BIT(4) /* UTIE */ | BIT(7) /* MTIE */)

static RISCVException esp_cpu_mie_csr_read(CPURISCVState *env, int csrno,
                                           target_ulong *ret_value)
{
    EspRISCVCPU *s = esp_cpu_riscv_to_cpu(env);
    *ret_value = s->mie_enabled;
    return RISCV_EXCP_NONE;
}

static RISCVException esp_cpu_mie_csr_write(CPURISCVState *env, int csrno,
                                            target_ulong new_value)
{
    EspRISCVCPU *s = esp_cpu_riscv_to_cpu(env);
    s->mie_enabled = (uint32_t) new_value;
    /* Only the standard CLINT bits propagate into env->mie; the rest
     * (MXIE per-line enables on the C6) live solely in mie_enabled and
     * are honoured by the intmatrix.  MEIE (bit 11) is forced on so the
     * parent dispatcher's `mie & mip` check accepts external IRQs that
     * the intmatrix has already validated. */
    env->mie &= ~ESP_CPU_MIE_CLINT_MASK;
    env->mie |= (new_value & ESP_CPU_MIE_CLINT_MASK) | MIP_MEIP;
    if (s->mie_changed_cb) {
        s->mie_changed_cb(s->mie_changed_opaque);
    }
    return RISCV_EXCP_NONE;
}

static riscv_csr_operations esp_cpu_mie_csr_ops = {
    .predicate = esp_cpu_csr_predicate,
    .read = esp_cpu_mie_csr_read,
    .write = esp_cpu_mie_csr_write,
};

void esp_cpu_set_mie_changed_cb(EspRISCVCPU *cpu,
                                void (*cb)(void *opaque),
                                void *opaque)
{
    cpu->mie_changed_cb = cb;
    cpu->mie_changed_opaque = opaque;
}


static void esp_cpu_update_parent_irq(EspRISCVCPU *cpu)
{
    if (cpu->irq_lines != 0) {
        qemu_irq_raise(cpu->parent_irq);
    } else {
        qemu_irq_lower(cpu->parent_irq);
    }
}

/**
 * Function called when an interrupt is incoming.
 */
static void esp_cpu_irq_handler(void *opaque, int n, int level)
{
    EspRISCVCPU *cpu = (EspRISCVCPU*) opaque;

    /* Lines go from 1 to 31 included */
    assert(n <= ESP_CPU_INT_LINES);

    if (n == 0) {
        return;
    }

    if (level != 0) {
        SET_BIT(cpu->irq_lines, n);
    } else {
        CLEAR_BIT(cpu->irq_lines, n);
    }

    esp_cpu_update_parent_irq(cpu);
}


static uint32_t esp_cpu_select_irq_cause(EspRISCVCPU *cpu)
{
    for (uint32_t i = 1; i <= ESP_CPU_INT_LINES; i++) {
        if (BIT_SET(cpu->irq_lines, i)) {
            return i;
        }
    }

    return 0;
}

/**
 * On ESP32-C6 the MIE CSR is repurposed as a per-line enable bitmask, so the
 * standard RISC-V MIE.MEIE bit (11) is never set.  The default has_work /
 * cpu_exec_halt check (mip & mie) would therefore fail, causing WFI to
 * never wake even when an external interrupt is pending.
 */
static bool esp_cpu_has_work(CPUState *cs)
{
    EspRISCVCPU *cpu = ESP_CPU(cs);

    if (cpu->irq_lines != 0 || (cs->interrupt_request & CPU_INTERRUPT_HARD)) {
        return true;
    }

    EspRISCVCPUClass *klass = ESP_CPU_GET_CLASS(cpu);
    return klass->parent_has_work(cs);
}


/**
 * TCG operation called when the CPU has to actually jump to the interrupt handler.
 */
static bool esp_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    /* We could re-implement the whole interrupt process from here.
     * The simplest solution however is to call the parent's implementation and
     * replace the most important part for us: the mcause. */
    EspRISCVCPU *cpu = ESP_CPU(cs);
    EspRISCVCPUClass *klass = ESP_CPU_GET_CLASS(cpu);
    const uint32_t cause = esp_cpu_select_irq_cause(cpu);

    if (cause == 0) {
        return false;
    }

    /*
     * Bridge our intmatrix-gated model to the parent RISC-V dispatcher's
     * `mie & mip` check on IRQ_M_EXT.
     *
     * On the ESP32-C3, mie keeps its standard layout and the C3 intmatrix
     * preloads MEIE in env->mie at reset; the parent's check therefore
     * succeeds without any further help from us, and respecting whatever
     * the guest later writes to mie keeps standard masking semantics.
     *
     * On the ESP32-C6, mie is repurposed as a per-line MXIE bitmap so the
     * standard MEIE meaning is gone.  Our esp_cpu_mie_csr_write already
     * keeps env->mie's bit 11 set after every guest write, but during the
     * narrow window between the stock RISC-V cpu_reset (which clears
     * env->mie) and the first guest mie write, env->mie can be 0.  Force
     * MEIE here as a safety net so any IRQ the intmatrix raises during
     * that window is still delivered.
     */
    CPURISCVState *env = &cpu->parent_obj.env;
    target_ulong saved_mie = 0;
    if (cpu->mie_as_bitmap) {
        saved_mie = env->mie;
        env->mie |= MIP_MEIP;
    }

    const bool accepted = klass->parent_exec_interrupt(cs, interrupt_request);

    if (cpu->mie_as_bitmap) {
        env->mie = saved_mie;
    }

    if (accepted) {
        const bool vectored = (env->mtvec & 3) == 1;

        /* Update the mcause and the relevant PC */
        env->mcause = RISCV_EXCP_INT_FLAG | cause;

        /* Recalculate the PC thanks to the cause */
        env->pc = (env->mtvec >> 2 << 2) + (vectored ? cause * 4 : 0);
    }

    /* Similarly, make sure the parent IRQ reflects the current state */
    return accepted;
}


/**
 * Taken from `cpu.c`, as this function is private in that file
 */
static void set_misa(CPURISCVState *env, RISCVMXL mxl, uint32_t ext)
{
    env->misa_ext_mask = env->misa_ext = ext;
}

static void esp_cpu_reset(void *opaque)
{
    EspRISCVCPU *cpu = opaque;
    cpu->irq_lines = 0;
    cpu->mie_enabled = 0;
    qemu_irq_lower(cpu->parent_irq);
    cpu_reset(CPU(cpu));
}

static void esp_cpu_realize(DeviceState *dev, Error **errp)
{
    EspRISCVCPU *espcpu = ESP_CPU(dev);
    EspRISCVCPUClass *klass = ESP_CPU_GET_CLASS(dev);

    espcpu->parent_obj.env.mhartid = espcpu->hartid_base;
    qemu_register_reset(esp_cpu_reset, espcpu);

    klass->parent_realize(dev, errp);

    if (riscv_cpu_claim_interrupts(&espcpu->parent_obj, MIP_MEIP) < 0) {
        error_report("MIP_MEIP already claimed");
        exit(1);
    }

    /* PMA (Physical Memory Attribute) CSRs: Espressif extension used by the
     * ESP32-C6 (and later) IDF startup code to configure memory region access
     * permissions.  Only register them when the SOC is known to support PMA,
     * since the ESP32-C3 also uses this `esp_cpu` implementation but doesn't
     * implement PMA in hardware.
     * pmacfg0-15 at 0xBC0-0xBCF, pmaaddr0-15 at 0xBD0-0xBDF */
    if (espcpu->has_pma) {
        for (int i = 0xBC0; i <= 0xBDF; i++) {
            riscv_set_csr_ops(i, &esp_cpu_csr_ops);
        }
    }

    /* On SOCs that repurpose mie as a per-line external-interrupt enable
     * bitmap (ESP32-C6 and later), install our custom mie CSR ops.  Done
     * here in realize (not init) so the C3, which uses the same EspRISCVCPU
     * type but with mie_as_bitmap=false, retains the stock RISC-V semantics
     * for mie.MEIE. */
    if (espcpu->mie_as_bitmap) {
        riscv_set_csr_ops(CSR_MIE, &esp_cpu_mie_csr_ops);
    }
}

static struct TCGCPUOps tcg_ops = { 0 };

static void esp_cpu_override_tcg_interrupts(Object *obj)
{
    EspRISCVCPUClass *klass = ESP_CPU_GET_CLASS(obj);
    CPUClass *cc = CPU_CLASS(klass);
    EspRISCVCPUClass *cpuclass = ESP_CPU_CLASS(klass);

    /* The goal of this RISC-V CPU child class is to override the way interrupts are handled.
     * In theory, it would be enough to override `do_interrupt` function from the CPU's TCGCPUOps
     * structure, however, in practice, we have to override `riscv_cpu_exec_interrupt` function.
     * This is due to the fact that the RISC-V implementation doesn't call the `do_interrupt` routine
     * from its TCGCPUOps routine, but directly calls its `riscv_cpu_do_interrupt` function.
     * As that structure may be constant, we have to copy it in order to replace one of its field. */
    memcpy(&tcg_ops, cc->tcg_ops, sizeof(struct TCGCPUOps));

    /* Copy the parent's exec_interrupt function as we will execute it later */
    cpuclass->parent_exec_interrupt = tcg_ops.cpu_exec_interrupt;

    /* Replace with our overridden implementations */
    tcg_ops.cpu_exec_interrupt = esp_cpu_exec_interrupt;
    tcg_ops.cpu_exec_halt = esp_cpu_has_work;
    cc->tcg_ops = &tcg_ops;
}

static void esp_cpu_init(Object *obj)
{
    EspRISCVCPU *s = ESP_CPU(obj);
    RISCVCPU *cpu = RISCV_CPU(obj);
    CPURISCVState *env = &cpu->env;
    set_misa(env, MXL_RV32, RVI | RVM | RVC);
    /* Zawrs extension is enabled by default, but depends on "A" extension which isn't present on C3 */
    cpu->cfg.ext_zawrs = false;
    /* Zfa extension is enabled by default, but depends on "F" extension which isn't present on C3 */
    cpu->cfg.ext_zfa = false;

    /* Since the TCG operations are now separated from the standard RISC-V CPU, we have to override
     * the TCG operations in this init function instead of the class init */
    esp_cpu_override_tcg_interrupts(obj);

    /* Initialize the IRQ lines */
    qdev_init_gpio_in_named_with_opaque(DEVICE(s),
                                        esp_cpu_irq_handler, s,
                                        ESP_CPU_IRQ_LINES_NAME, ESP_CPU_INT_LINES + 1);

    /* Initialize the parent IRQ line that will be used to notify the parent class when an interrupt
     * request is incoming. */
    s->parent_irq = qdev_get_gpio_in(DEVICE(s), IRQ_M_EXT);

    /* Set the user operations: ESP32-C3/C6 ROMs write to User Trap Setup CSRs
     * (ustatus, uie, utvec) and User Trap Handling CSRs during early boot.
     * Register all of them so they don't trigger illegal instruction exceptions. */
    riscv_set_csr_ops(CSR_USTATUS, &esp_cpu_csr_ops);
    riscv_set_csr_ops(CSR_UIE, &esp_cpu_csr_ops);
    riscv_set_csr_ops(CSR_UTVEC, &esp_cpu_csr_ops);
    riscv_set_csr_ops(CSR_USCRATCH, &esp_cpu_csr_ops);
    riscv_set_csr_ops(CSR_UEPC, &esp_cpu_csr_ops);
    riscv_set_csr_ops(CSR_UCAUSE, &esp_cpu_csr_ops);
    riscv_set_csr_ops(CSR_UTVAL, &esp_cpu_csr_ops);
    riscv_set_csr_ops(CSR_UIP, &esp_cpu_csr_ops);

    /* Override debug CSRs as they are not all supported by QEMU's RISC-V core */
    for (int i = ESP_CPU_CSR_TSELECT; i <= ESP_CPU_CSR_TCONTROL; i++) {
        riscv_set_csr_ops(i, &esp_cpu_csr_ops);
    }

    /* Register all non-standard Control and Status registers */
    riscv_set_csr_ops(ESP_CPU_CSR_PCER_M, &esp_cpu_csr_ops);
    riscv_set_csr_ops(ESP_CPU_CSR_PCMR_M, &esp_cpu_csr_ops);
    riscv_set_csr_ops(ESP_CPU_CSR_MCYCLE_M, &esp_cpu_csr_ops);

    riscv_set_csr_ops(ESP_CPU_CSR_PCER_U, &esp_cpu_csr_ops);
    riscv_set_csr_ops(ESP_CPU_CSR_PCMR_U, &esp_cpu_csr_ops);
    riscv_set_csr_ops(ESP_CPU_CSR_MCYCLE_U, &esp_cpu_csr_ops);

    s->cc_machine = (ESPCPUCycleCounter) {
        .divider = 6250,   /* 6.25ns per instruction at 160MHz. */
    };
    s->cc_user = (ESPCPUCycleCounter) {
        .divider = 6250,   /* Should be using the target configured CPU clock frequency instead. */
    };
}

static Property riscv_harts_props[] = {
    DEFINE_PROP_UINT32("hartid-base", EspRISCVCPU, hartid_base, 0),
    DEFINE_PROP_BOOL("has-pma", EspRISCVCPU, has_pma, false),
    DEFINE_PROP_BOOL("mie-as-bitmap", EspRISCVCPU, mie_as_bitmap, false),
    DEFINE_PROP_END_OF_LIST(),
};


static void esp_cpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CPUClass *cc = CPU_CLASS(klass);
    EspRISCVCPUClass *cpuclass = ESP_CPU_CLASS(klass);

    device_class_set_props(dc, riscv_harts_props);
    /* Save the parent realize function in order to be able to call it later */
    device_class_set_parent_realize(dc, esp_cpu_realize,
                                    &cpuclass->parent_realize);

    /* Override has_work so the CPU can wake from WFI with our custom
     * interrupt mechanism (MIE CSR is repurposed on C6). */
    cpuclass->parent_has_work = cc->has_work;
    cc->has_work = esp_cpu_has_work;
}

static const TypeInfo esp_cpu_info = {
    .name = TYPE_ESP_RISCV_CPU,
    .parent = TYPE_RISCV_CPU_BASE32,
    .instance_size = sizeof(EspRISCVCPU),
    .instance_align = __alignof__(EspRISCVCPU),
    .instance_init = esp_cpu_init,
    .class_size = sizeof(EspRISCVCPUClass),
    .class_init = esp_cpu_class_init,
};

static void esp_cpu_register_type(void)
{
    type_register_static(&esp_cpu_info);
}

type_init(esp_cpu_register_type)

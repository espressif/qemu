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
        /* Let's say that we have 1 instruction/clock cycle, so 1 instruction/6.25ns */
        assert(cc->divider != 0);
        diff = (now - cc->former_time) / cc->divider;
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
 * Checks whether the CPU can accepts interrupts or not
 */
bool esp_cpu_accept_interrupts(EspRISCVCPU *cpu)
{
    /* Get the MIE bit out of the MSTATUS register */
    CPURISCVState *env = &cpu->parent_obj.env;
    const bool mie = (riscv_csr_read(env, CSR_MSTATUS) & MSTATUS_MIE) != 0;

    return !cpu->irq_pending && mie;
}


/**
 * Function called when an interrupt is incoming.
 */
static void esp_cpu_irq_handler(void *opaque, int n, int level)
{
    EspRISCVCPU *cpu = (EspRISCVCPU*) opaque;

    /* Interrupt incoming if level is not 0, make sure we can receive interrupts */
    if (level && esp_cpu_accept_interrupts(cpu)) {
        cpu->irq_pending = true;
        cpu->irq_cause = n;
        qemu_irq_raise(cpu->parent_irq);
    }
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

    const bool accepted = klass->parent_exec_interrupt(cs, interrupt_request);

    if (accepted) {
        CPURISCVState *env = &cpu->parent_obj.env;
        const bool vectored = (env->mtvec & 3) == 1;
        const uint32_t cause = cpu->irq_cause;

        /* IRQ has been acknowledged by the parent CPU, it is not pending anymore */
        cpu->irq_pending = false;
        qemu_irq_lower(cpu->parent_irq);

        /* Update the mcause and the relevant PC */
        env->mcause = RISCV_EXCP_INT_FLAG | cause;

        /* Recalculate the PC thanks to the cause */
        env->pc = (env->mtvec >> 2 << 2) + (vectored ? cause * 4 : 0);
    }

    return accepted;
}


/**
 * Taken from `cpu.c`, as this function is private in that file
 */
static void set_misa(CPURISCVState *env, RISCVMXL mxl, uint32_t ext)
{
    env->misa_mxl_max = env->misa_mxl = mxl;
    env->misa_ext_mask = env->misa_ext = ext;
}

static void esp_cpu_reset(void *opaque)
{
    EspRISCVCPU *cpu = opaque;
    cpu->irq_pending = 0;
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

    /* Initialize the IRQ lines */
    qdev_init_gpio_in_named_with_opaque(DEVICE(s),
                                        esp_cpu_irq_handler, s,
                                        ESP_CPU_IRQ_LINES_NAME, ESP_CPU_INT_LINES + 1);

    /* Initialize the parent IRQ line that will be used to notify the parent class when an interrupt
     * request is incoming. */
    s->parent_irq = qdev_get_gpio_in(DEVICE(s), IRQ_M_EXT);

    /* Set the user operations */
    riscv_set_csr_ops(CSR_USTATUS, &esp_cpu_csr_ops);

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

    /* Re-use the macro that checks and casts any generic/parent class to the real child instance */
    s->cc_machine = (ESPCPUCycleCounter) {
        .divider = 6,   /* 6.25ns per instruction at 160MHz. */
    };
    s->cc_user = (ESPCPUCycleCounter) {
        .divider = 6,   /* Should be using the target configured CPU clock frequency instead. */
    };
}

static Property riscv_harts_props[] = {
    DEFINE_PROP_UINT32("hartid-base", EspRISCVCPU, hartid_base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static struct TCGCPUOps tcg_ops = { 0 };

static void esp_cpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CPUClass *cc = CPU_CLASS(klass);
    EspRISCVCPUClass *cpuclass = ESP_CPU_CLASS(klass);

    device_class_set_props(dc, riscv_harts_props);
    /* Save the parent realize function in order to be able to call it later */
    device_class_set_parent_realize(dc, esp_cpu_realize,
                                    &cpuclass->parent_realize);
    // device_class_set_parent_reset(dc, esp_cpu_reset, &cpuclass->parent_reset);

    /* The goal of this RISC-V CPU child class is to override the way interrupts are handled.
     * In theory, it would be enough to override `do_interrupt` function from the CPU's TCGCPUOps
     * structure, however, in practice, we have to override `riscv_cpu_exec_interrupt` function.
     * This is due to the fact that the RISC-V implementation doesn't call the `do_interrupt` routine
     * from its TCGCPUOps routine, but directly calls its `riscv_cpu_do_interrupt` function.
     * As that structure may be constant, we have to copy it in order to replace one of its field. */
    memcpy(&tcg_ops, cc->tcg_ops, sizeof(struct TCGCPUOps));

    /* Copy the parent's exec_interrupt function as we will execute it later */
    cpuclass->parent_exec_interrupt = tcg_ops.cpu_exec_interrupt;

    /* Replace it with our overriden implementation */
    tcg_ops.cpu_exec_interrupt = esp_cpu_exec_interrupt;
    cc->tcg_ops = &tcg_ops;
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

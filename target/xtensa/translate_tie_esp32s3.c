#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-temp-internal.h"
#include "qemu/log.h"
#include "qemu/qemu-print.h"
#include "exec/cpu_ldst.h"
#include "semihosting/semihost.h"
#include "exec/translator.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/log.h"

#include "translate.h"
#include "cpu_esp32s3.h"

typedef enum Addr_Update
{
    addr_nop,
    addr_inc16,
    addr_dec16,
    addr_ip,
    addr_xp,
    addr_ldbc_inc1,
} Addr_Update;

typedef enum vldbc_type
{
    vldbc_8,
    vldbc_16,
    vldbc_32,
} vldbc_type;

typedef enum ldqa_type
{
    ldqa_u8,
    ldqa_s8,
    ldqa_u16,
    ldqa_s16,
    ldqa_u32,
    ldqa_s32,
} ldqa_type;

typedef enum vmul_type
{
    vmul_nop,
    vmul_s8,
    vmul_u8,
    vmul_s16,
    vmul_u16,
    vmul_s8xs8,
    vmul_s8lx16,
    vmul_s8hx16,
    vmul_s32,
    vmul_qup,
}vmul_type;

typedef enum ee_ldst_type
{
    ee_load_op,
    ee_store_op,
} ee_ldst_type;


typedef enum cmul_type
{
    cmul_sel0,
    cmul_sel1,
    cmul_sel2,
    cmul_sel3,
    
}cmul_type;

typedef enum wrur_type
{
    wrur_accx_0,
    wrur_accx_1,
    wrur_qacc_h_0,
    wrur_qacc_h_1,
    wrur_qacc_h_2,
    wrur_qacc_h_3,
    wrur_qacc_h_4,
    wrur_qacc_l_0,
    wrur_qacc_l_1,
    wrur_qacc_l_2,
    wrur_qacc_l_3,
    wrur_qacc_l_4,
    wrur_gpio_out,
    wrur_sar_byte,
    wrur_fft_bit_width,
    wrur_ua_state_0,
    wrur_ua_state_1,
    wrur_ua_state_2,
    wrur_ua_state_3,
} wrur_type;

typedef enum ee_zero_type
{
    ee_zero_qacc,
    ee_zero_accx,
    ee_zero_qx,

}ee_zero_type;

typedef enum ee_movi_type
{
    ee_movi_a,
    ee_movi_q,
}ee_movi_type;

typedef enum ee_vcmp_type
{
    ee_vcmp_eq,
    ee_vcmp_lt,
    ee_vcmp_gt,
}ee_vcmp_type;

typedef enum ee_bw_logic_type
{
    bw_logic_or,
    bw_logic_and,
    bw_logic_xor,
    bw_logic_not,
    bw_shift_left,
    bw_shift_right,
}ee_bw_logic_type;

typedef enum gpio_type
{
    gpio_mask,
    gpio_set,
    gpio_clr,
    gpio_in,
} gpio_type;


typedef struct QACC_reg_s
{
    union
    {
        int32_t  s20[8];
        uint32_t u20[8];
        uint8_t  u20_8[32];
    };
    union
    {
        int64_t  s40[4];
        uint64_t u40[4];
        uint8_t  u40_8[32];
    };
} QACC_reg;

typedef enum ee_arithmetic_type
{
    ee_add_op,
    ee_sub_op,
}ee_arithmetic_type;

extern TCGv_i32 cpu_SR[256];

static void load_qacc(uint8_t*  src, QACC_reg* dest);
static void save_qacc20(QACC_reg* src, uint8_t* dest);
static void save_qacc40(QACC_reg* src, uint8_t* dest);

static void load_qacc(uint8_t* src, QACC_reg* dest)
{
    for (int i=0 ; i< 4 ; i++)
    {
        dest->s20[0 + i*2] = ((((uint32_t)src[2 + i*5]) << 16) |(((uint32_t)src[1 + i*5]) << 8) | ((uint32_t)src[0 + i*5])) << 12;
        dest->s20[0 + i*2] = dest->s20[0 + i*2] >> 12;

        dest->s20[1 + i*2] = ((((uint32_t)src[4 + i*5]) << 12) |(((uint32_t)src[3 + i*5]) << 4) | ((uint32_t)src[2 + i*5] >> 4)) << 12;
        dest->s20[1 + i*2] = dest->s20[1 + i*2] >> 12;
    }

    for (int i=0 ; i< 4 ; i++)
    {
        dest->s40[i] = ((((uint64_t)src[4 + i*5]) << 32) | (((uint64_t)src[3 + i*5]) << 24)| (((uint64_t)src[2 + i*5]) << 16)| (((uint64_t)src[1 + i*5]) << 8) | ((uint64_t)src[0 + i*5])) << 24;
        dest->s40[i] = dest->s40[i] >> 24;
    }
}
static void save_qacc20(QACC_reg* src, uint8_t* dest)
{
    for (int i = 0; i < 4; i++)
    {
        dest[0 + i*5] = (uint8_t)((src->u20[0 + i*2]) & 0xff);
        dest[1 + i*5] = (uint8_t)((src->u20[0 + i*2]>>8) & 0xff);
        dest[2 + i*5] = (uint8_t)(((src->u20[0 + i*2]>>16) & 0xf) | ((src->u20[1 + i*2]<<4) & 0xf0));
        dest[3 + i*5] = (uint8_t)((src->u20[1 + i*2]>>4) & 0xff);
        dest[4 + i*5] = (uint8_t)((src->u20[1 + i*2]>>12) & 0xff);
    }
}
static void save_qacc40(QACC_reg* src, uint8_t* dest)
{
    for (int i = 0; i < 4; i++)
    {
        uint8_t* src_ptr = (uint8_t*)&src->u40[i];
        for (int m = 0; m < 5; m++)
        {
            dest[i*5 + m] = src_ptr[m];
        }
    }
}

static inline esp_qreg_t *cpu_vec_ptr(CPUXtensaState *env, int i)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    return &tie->Q[i];
}

static inline uint8_t *cpu_sar_ptr(CPUXtensaState *env)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    return &tie->SAR_BYTE;
}

static inline ACCQ_reg *cpu_qacc_ptr(CPUXtensaState *env, int a_b)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    return &tie->ACCQ[a_b];
}

void HELPER(vld_64_s3)(CPUXtensaState *env, uint32_t vec, uint64_t data, uint32_t low_high)
{
    esp_qreg_t *avr = cpu_vec_ptr(env, vec);
    avr->u64[low_high] = data;
    return;
}

void HELPER(ldqa_64_s3)(CPUXtensaState *env, uint32_t data_type, uint64_t data, uint32_t low_high)
{
    ACCQ_reg *qacc = cpu_qacc_ptr(env, low_high);
    if(ldqa_u8 == (ldqa_type)data_type)
    {
        uint8_t* data_ptr = (uint8_t*)&data;
        QACC_reg q_reg;
        for (int i=0 ; i< 8 ; i++)
        {
            q_reg.u20[i] = (uint32_t)data_ptr[i]; 
        }
        save_qacc20(&q_reg, qacc->u8);
    } else if(ldqa_s8 == (ldqa_type)data_type)
    {
        int8_t* data_ptr = (int8_t*)&data;
        QACC_reg q_reg;
        for (int i=0 ; i< 8 ; i++)
        {
            q_reg.s20[i] = (int32_t)data_ptr[i]; 
        }
        save_qacc20(&q_reg, qacc->u8);
    } else if(ldqa_u16 == (ldqa_type)data_type)
    {
        uint16_t* data_ptr = (uint16_t*)&data;
        QACC_reg q_reg;
        for (int i=0 ; i< 4 ; i++)
        {
            q_reg.u40[i] = (uint32_t)data_ptr[i]; 
        }
        save_qacc40(&q_reg, qacc->u8);
    } else if(ldqa_s16 == (ldqa_type)data_type)
    {
        int16_t* data_ptr = (int16_t*)&data;
        QACC_reg q_reg;
        for (int i=0 ; i< 4 ; i++)
        {
            q_reg.s40[i] = (int32_t)data_ptr[i]; 
        }
        save_qacc40(&q_reg, qacc->u8);
    }
    return;
}

void HELPER(set_sar_byte_s3)(CPUXtensaState *env, uint32_t sar)
{
    uint8_t* sar_ptr = cpu_sar_ptr(env);
    *sar_ptr = sar;
    return;
}


void HELPER(vldhbc_16_s3)(CPUXtensaState *env, uint32_t vec, uint64_t data)
{
    esp_qreg_t *avr = cpu_vec_ptr(env, vec);
    uint16_t* data16 = (uint16_t*)&data;
    for (int i = 0; i < 4; i++)
    {
        avr->u16[i*2 + 0] = data16[i];
        avr->u16[i*2 + 1] = data16[i];
    }
    return;
}

void HELPER(vldbc_s3)(CPUXtensaState *env, uint32_t vec, uint32_t data, uint32_t bc_bits)
{
    esp_qreg_t *avr = cpu_vec_ptr(env, vec);
    if (vldbc_8 == bc_bits)
    {
        for (int i=0 ; i< 16 ; i++)
        {
            avr->u8[i] = data&0xff;
        }
    } else if (vldbc_16 == bc_bits)
    {
        for (int i=0 ; i< 8 ; i++)
        {
            avr->u16[i] = data&0xffff;
        }
    } else if (vldbc_32 == bc_bits)
    {
        for (int i=0 ; i< 4 ; i++)
        {
            avr->u32[i] = data;
        }
    } else
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension! \n");
    }
    return;
}

uint64_t HELPER(vst_64_s3)(CPUXtensaState *env, uint32_t vec, uint32_t low_high)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = tie->Q[vec].u64[low_high];
    return result;
}

/// @brief  This function used to dump the code from QEMU
void HELPER(dump_all_s3)(CPUXtensaState *env)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    printf("Dump TIE: \n");
    for (int i = 0; i < 8; i++)
    {
        printf("Q[%i]: ", i);
        esp_qreg_t *avr = cpu_vec_ptr(env, i);

        for (int m = 0; m < 16; m++)
        {
            printf("%2.2x ", avr->u8[m]);
        }
        printf("\n");
        /* code */
    }
    QACC_reg q_req_l;
    QACC_reg q_req_h;
    load_qacc(tie->ACCQ[0].u8, &q_req_l);
    load_qacc(tie->ACCQ[1].u8, &q_req_h);
    printf("QACC_L: ");
    for (int i = 0; i < 20; i++)
    {
        printf("%2.2x ", tie->ACCQ[0].u8[i]);
    }
    printf("\n");
    printf("QACC_H: ");
    for (int i = 0; i < 20; i++)
    {
        printf("%2.2x ", tie->ACCQ[1].u8[i]);
    }
    printf("\n");
    
    for (int i = 0; i < 8; i++)
    {
        printf("QACC_L S20[%i] %8.8x\n", i, q_req_l.s20[i]);
    }
    for (int i = 0; i < 8; i++)
    {
        printf("QACC_H S20[%i] %8.8x\n", i+8, q_req_h.s20[i]);
    }
    
    for (int i = 0; i < 4; i++)
    {
        printf("QACC_L S40[%i] %16.16" PRIx64 "\n", i, q_req_l.s40[i]);
    }
    for (int i = 0; i < 4; i++)
    {
        printf("QACC_H S40[%i] %16.16"  PRIx64 "\n", i+4, q_req_h.s40[i]);
    }
    printf("ACCX = %16.16"  PRIx64 "\n", tie->ACCX);
    printf("SAR_BYTE = %i\n", tie->SAR_BYTE);

    printf("\n");
    return;
}

static void translate_zero(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    // Example, how to call dump for all registers
    // Dump all 
    // translate_dump_all_s3(dc,arg,par);
    // Clear acc:

    TCGv_i32 index;
    index = tcg_constant_i32(par[0]);

    if ((ee_zero_qacc == (ee_zero_type)par[0]) || (ee_zero_accx == (ee_zero_type)par[0]))
    {
        TCGv_i32 dummy = tcg_constant_i32(0);
        gen_helper_zero_s3(tcg_env, index, dummy);
        tcg_temp_free_i32(dummy);

    } else
    {
        TCGv_i32 qr_index = tcg_constant_i32((uint32_t)arg[0].imm);
        gen_helper_zero_s3(tcg_env, index, qr_index);
        tcg_temp_free_i32(qr_index);
    }

    tcg_temp_free_i32(index);
}


void HELPER(zero_s3)(CPUXtensaState *env, uint32_t data, uint32_t index)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if ((ee_zero_type)data == ee_zero_qacc)
    {
        for (int i = 0; i < 20; i++)
        {
            tie->ACCQ[0].u8[i] = 0;
            tie->ACCQ[1].u8[i] = 0;
        }
    } else if ((ee_zero_type)data == ee_zero_accx)
    {
            tie->ACCX = 0;
    } else if ((ee_zero_type)data == ee_zero_qx)
    {
            tie->Q[index].u64[0] = 0;
            tie->Q[index].u64[1] = 0;
    }
}

void HELPER(wur_s3)(CPUXtensaState *env, uint32_t data, uint32_t wrur_reg)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    switch ((wrur_type)wrur_reg)
    {
    case wrur_accx_0:
        ((uint32_t*)(&tie->ACCX))[0] = data;
        tie->ACCX &= 0x000000ffffffffff;
        break;
    case wrur_accx_1:
        ((uint32_t*)(&tie->ACCX))[1] = data;
        tie->ACCX &= 0x000000ffffffffff;
        break;
    case wrur_qacc_l_0:
    case wrur_qacc_l_1:
    case wrur_qacc_l_2:
    case wrur_qacc_l_3:
    case wrur_qacc_l_4:
        {
            uint8_t* data_ptr = (uint8_t*)&data;
            int index = wrur_reg - wrur_qacc_l_0; 
            for (int i = 0; i < 4; i++)
            {
                tie->ACCQ[0].u8[i + 4*index] = data_ptr[i];
            }
        }
        break;
    case wrur_qacc_h_0:
    case wrur_qacc_h_1:
    case wrur_qacc_h_2:
    case wrur_qacc_h_3:
    case wrur_qacc_h_4:
        {
            uint8_t* data_ptr = (uint8_t*)&data;
            int index = wrur_reg - wrur_qacc_h_0; 
            for (int i = 0; i < 4; i++)
            {
                tie->ACCQ[1].u8[i + 4*index] = data_ptr[i];
            }
        }
        break;
    case wrur_gpio_out:
        tie->gpio_out = data;
        break;
    case wrur_sar_byte:
        tie->SAR_BYTE = data;
        break;
    case wrur_fft_bit_width:
        tie->fft_width = data;
        break;
    case wrur_ua_state_0:
    case wrur_ua_state_1:
    case wrur_ua_state_2:
    case wrur_ua_state_3:
        {
            int index = wrur_reg - wrur_ua_state_0;
            tie->UA_STATE.u32[index] = data;
        }
        break;
    default:
        break;
    }
    return;
}

static void translate_wur(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 wur_addr = tcg_constant_i32(par[0]);
    
    gen_helper_wur_s3(tcg_env, arg[0].in, wur_addr);
    
    tcg_temp_free_i32(wur_addr);
}

uint32_t HELPER(rur_s3)(CPUXtensaState *env, uint32_t wrur_reg)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint32_t result = 0;
    switch ((wrur_type)wrur_reg)
    {
    case wrur_accx_0:
        result = ((uint32_t*)(&tie->ACCX))[0];
        break;
    case wrur_accx_1:
        result = ((uint32_t*)(&tie->ACCX))[1];
        break;
    case wrur_qacc_l_0:
    case wrur_qacc_l_1:
    case wrur_qacc_l_2:
    case wrur_qacc_l_3:
    case wrur_qacc_l_4:
        {
            uint8_t* data_ptr = (uint8_t*)&result;
            int index = wrur_reg - wrur_qacc_l_0;
            for (int i = 0; i < 4; i++)
            {
                data_ptr[i] = tie->ACCQ[0].u8[i + 4*index];
            }
        }
        break;
    case wrur_qacc_h_0:
    case wrur_qacc_h_1:
    case wrur_qacc_h_2:
    case wrur_qacc_h_3:
    case wrur_qacc_h_4:
        {
            uint8_t* data_ptr = (uint8_t*)&result;
            int index = wrur_reg - wrur_qacc_h_0;
            for (int i = 0; i < 4; i++)
            {
                data_ptr[i] = tie->ACCQ[1].u8[i + 4*index];
            }
        }
        break;
    case wrur_gpio_out:
        result = tie->gpio_out;
        break;
    case wrur_sar_byte:
        result = tie->SAR_BYTE;
        break;
    case wrur_fft_bit_width:
        result = tie->fft_width;
        break;
    case wrur_ua_state_0:
    case wrur_ua_state_1:
    case wrur_ua_state_2:
    case wrur_ua_state_3:
        {
            int index = wrur_reg - wrur_ua_state_0;
            result = tie->UA_STATE.u32[index];
        }
        break;
    default:
        break;
    }

    return result;
}

static void translate_rur(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 rur_addr = tcg_constant_i32(par[0]);
    
    gen_helper_rur_s3( arg[0].out, tcg_env, rur_addr);
    
    tcg_temp_free_i32(rur_addr);
}

static void translate_vld_64_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    // We have to align to 128 bit memory
    tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff8);

    MemOp mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

    TCGv_i64 data = tcg_temp_new_i64();
    // Read data from memory
    tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

    TCGv_i32 index;
    index = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 low_high = tcg_constant_i32(par[1]);

    gen_helper_vld_64_s3(tcg_env, index, data, low_high);

    if (par[0] == addr_ip)
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    } else if (par[0] == addr_xp)
    {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    } else 
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
    tcg_temp_free_i64(data);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(index);
    tcg_temp_free_i32(low_high);
}

static void translate_vldhbc_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i< 2 ; i++)
    {
        TCGv_i32 addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[2].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);

        MemOp mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[i].imm);

        gen_helper_vldhbc_16_s3(tcg_env, index, data);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
    }

    tcg_gen_addi_i32(arg[2].out, arg[2].in, 16);
}

static void translate_vldbc_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    MemOp mop = MO_8;
    TCGv_i32 index;
    index = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 ldbc_type = tcg_constant_i32(par[1]);

    TCGv_i32 addr = tcg_temp_new_i32();

    if (vldbc_8 == par[1])
    {
        tcg_gen_andi_i32(addr, arg[1].in, 0xffffffff);
        mop = gen_load_store_alignment(dc, MO_8 | MO_TE, addr);
    } else if (vldbc_16 == par[1])
    {
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffffe);
        mop = gen_load_store_alignment(dc, MO_16 | MO_TE, addr);
    } else if (vldbc_32 == par[1])
    {
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffffc);
        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);
    } else 
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
    TCGv_i32 data = tcg_temp_new_i32();
    // Read data from memory
    tcg_gen_qemu_ld_i32(data, addr, dc->cring, mop);

    gen_helper_vldbc_s3(tcg_env, index, data, ldbc_type);

    tcg_temp_free_i32(data);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(index);

    if (par[0] == addr_ip)
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    } else if (par[0] == addr_xp)
    {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    } else if (par[0] ==addr_ldbc_inc1)
    {
        if (vldbc_8 == par[1]) tcg_gen_addi_i32(arg[1].out, arg[1].in, 1);
        if (vldbc_16 == par[1]) tcg_gen_addi_i32(arg[1].out, arg[1].in, 2);        
    } else if (par[0] != addr_nop)
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
}

static void translate_vst_64_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    // We have to align to 128 bit memory
    tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff8);

    MemOp mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

    TCGv_i64 data = tcg_temp_new_i64();

    TCGv_i32 index;
    index = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 low_high = tcg_constant_i32(par[1]);

    gen_helper_vst_64_s3(data, tcg_env, index, low_high);
    tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

    tcg_temp_free_i64(data);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(index);
    tcg_temp_free_i32(low_high);

    if (par[0] == addr_ip)
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    } else if (par[0] == addr_xp)
    {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    } else 
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
}

static void translate_vld_128_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_vld_64_s3(tcg_env, index, data, low_high);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
        tcg_temp_free_i32(low_high);
    }

    if (par[0] == addr_ip)
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    } else if (par[0] == addr_xp)
    {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    } else 
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
}

static void translate_ld_qr_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, arg[2].imm);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_vld_64_s3(tcg_env, index, data, low_high);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
        tcg_temp_free_i32(low_high);
    }
}

static void translate_ld_usar_128_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);
        
        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_vld_64_s3(tcg_env, index, data, low_high);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
        tcg_temp_free_i32(low_high);

    }

    TCGv_i32 sar = tcg_temp_new_i32();
    tcg_gen_andi_i32(sar, arg[1].in, 0xf);
    gen_helper_set_sar_byte_s3(tcg_env, sar);
    tcg_temp_free_i32(sar);

    if (par[0] == addr_ip)
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    } else if (par[0] == addr_xp)
    {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    } else 
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
}

static void translate_ldqa_128_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[0].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 data_type;
        data_type = tcg_constant_i32((uint32_t)par[1]);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_ldqa_64_s3(tcg_env, data_type, data, low_high);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(data_type);
        tcg_temp_free_i32(low_high);
    }

    if (par[0] == addr_ip)
    {
        tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);
    } else if (par[0] == addr_xp)
    {
        tcg_gen_add_i32(arg[0].out, arg[0].in, arg[1].in);
    } else 
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
}


static void translate_vst_128_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_vst_64_s3(data, tcg_env, index, low_high);
        tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
        tcg_temp_free_i32(low_high);
    }

    if (par[0] == addr_ip)
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    } else if (par[0] == addr_xp)
    {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    } else 
    {
        qemu_log_mask(LOG_GUEST_ERROR, "unknown instruction extension (pc = %08x)\n", dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
    }
}

static void translate_st_qr_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, arg[2].imm);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_vst_64_s3(data, tcg_env, index, low_high);
        tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
        tcg_temp_free_i32(low_high);
    }
}

void HELPER(mv_qr_s3)(CPUXtensaState *env, uint32_t qv, uint32_t qx)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    for (int i = 0; i < 16; i++)
    {
        tie->Q[qv].u8[i] = tie->Q[qx].u8[i];
    }    
}

static void translate_mv_qr_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
        TCGv_i32 qv = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[1].imm);

        gen_helper_mv_qr_s3(tcg_env, qv, qx);

        tcg_temp_free_i32(qv);
        tcg_temp_free_i32(qx);
}

uint64_t HELPER(fft_vst_64_s3)(CPUXtensaState *env, uint32_t qv, uint32_t sar2, uint32_t low_high)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int32_t* prt_result = (int32_t*)&result;
    if (0 == low_high)
    {
        prt_result[0] = tie->Q[qv].u32[3] >> sar2;
        prt_result[1] = tie->Q[qv].u32[2] >> sar2;
    } else 
    {
        prt_result[0] = tie->Q[qv].u32[1] >> sar2;
        prt_result[1] = tie->Q[qv].u32[0] >> sar2;
    }
    return result;
}

static void translate_fft_vst_decp_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 addr_low;
    TCGv_i32 addr_high;
    MemOp mop_low;
    MemOp mop_high;
    addr_low = tcg_temp_new_i32();
    addr_high = tcg_temp_new_i32();
    // We have to align to 128 bit memory
    tcg_gen_andi_i32(addr_low, arg[1].in, 0xfffffff0);
    tcg_gen_andi_i32(addr_high, arg[1].in, 0xfffffff0);
    tcg_gen_addi_i32(addr_high, addr_high, 8);

    mop_low = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_low);
    mop_high = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_high);

    TCGv_i64 data_low = tcg_temp_new_i64();
    TCGv_i64 data_high = tcg_temp_new_i64();
    // Read data from memory

    TCGv_i32 qv = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 sar2 = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 low_high = tcg_constant_i32(0);

    gen_helper_fft_vst_64_s3(data_low, tcg_env, qv, sar2, low_high);
    low_high = tcg_constant_i32(1);
    gen_helper_fft_vst_64_s3(data_high, tcg_env, qv, sar2, low_high);

    tcg_gen_qemu_st_i64(data_low, addr_low, dc->cring, mop_low);
    tcg_gen_qemu_st_i64(data_high, addr_high, dc->cring, mop_high);

    tcg_temp_free_i64(data_low);
    tcg_temp_free_i32(addr_low);
    tcg_temp_free_i64(data_high);
    tcg_temp_free_i32(addr_high);
    tcg_temp_free_i32(qv);
    tcg_temp_free_i32(low_high);

    tcg_gen_subi_i32(arg[1].out, arg[1].in, 16);
}

static void translate_ldf_128_ip(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    OpcodeArg arg32[4];
    for (int i = 0; i < 4; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[4].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

        get_f32_o1(arg, arg32, 3 - i);
        tcg_gen_qemu_ld_tl(arg32[3 - i].out, addr, dc->cring, mop);
        put_f32_o1(arg, arg32, 3 - i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_addi_i32(arg[4].out, arg[4].in, arg[5].imm);
}

static void translate_stf_128_ip(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    OpcodeArg arg32[4];
    for (int i = 0; i < 4; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[4].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

        get_f32_i1(arg, arg32, 3 - i);
        tcg_gen_qemu_st_tl(arg32[3 - i].in, addr, dc->cring, mop);
        put_f32_i1(arg, arg32, 3 - i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_addi_i32(arg[4].out, arg[4].in, arg[5].imm);
}

static void translate_ldf_64_ip(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    OpcodeArg arg32[2];
    for (int i = 0; i < 2; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 64 bit memory
        tcg_gen_andi_i32(addr, arg[2].in, 0xfffffff8);
        tcg_gen_addi_i32(addr, addr, i * 4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

        get_f32_o1(arg, arg32, 1 - i);
        tcg_gen_qemu_ld_tl(arg32[1 - i].out, addr, dc->cring, mop);
        put_f32_o1(arg, arg32, 1 - i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_addi_i32(arg[2].out, arg[2].in, arg[3].imm);
}

static void translate_stf_64_ip(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    OpcodeArg arg32[2];
    for (int i = 0; i < 2; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 64 bit memory
        tcg_gen_andi_i32(addr, arg[2].in, 0xfffffff8);
        tcg_gen_addi_i32(addr, addr, i * 4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

        get_f32_i1(arg, arg32, 1 - i);
        tcg_gen_qemu_st_tl(arg32[1 - i].in, addr, dc->cring, mop);
        put_f32_i1(arg, arg32, 1 - i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_addi_i32(arg[2].out, arg[2].in, arg[3].imm);
}

static void translate_ldf_128_xp(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
    OpcodeArg arg32[4];
    for (int i = 0; i < 4; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[4].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

        get_f32_o1(arg, arg32, 3 - i);
        tcg_gen_qemu_ld_tl(arg32[3 - i].out, addr, dc->cring, mop);
        put_f32_o1(arg, arg32, 3 - i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_add_i32(arg[4].out, arg[4].in, arg[5].in);
}

static void translate_stf_128_xp(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    OpcodeArg arg32[4];
    for (int i = 0; i < 4; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[4].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

        get_f32_i1(arg, arg32, 3 - i);
        tcg_gen_qemu_st_tl(arg32[3 - i].in, addr, dc->cring, mop);
        put_f32_i1(arg, arg32, 3 - i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_add_i32(arg[4].out, arg[4].in, arg[5].in);
}

static void translate_ldf_64_xp(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    OpcodeArg arg32[2];
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 64 bit memory
        tcg_gen_andi_i32(addr, arg[2].in, 0xfffffff8);
        tcg_gen_addi_i32(addr, addr, i*4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

            get_f32_o1(arg, arg32, 1-i);
            tcg_gen_qemu_ld_tl(arg32[1-i].out, addr, dc->cring, mop);
            put_f32_o1(arg, arg32, 1-i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_add_i32(arg[2].out, arg[2].in, arg[3].in);
}

static void translate_stf_64_xp(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    OpcodeArg arg32[2];
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;

        addr = tcg_temp_new_i32();

        // We have to align to 64 bit memory
        tcg_gen_andi_i32(addr, arg[2].in, 0xfffffff8);
        tcg_gen_addi_i32(addr, addr, i*4);

        mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

        get_f32_i1(arg, arg32, 1-i);
        tcg_gen_qemu_st_tl(arg32[1-i].in, addr, dc->cring, mop);
        put_f32_i1(arg, arg32, 1-i);

        tcg_temp_free_i32(addr);
    }
    tcg_gen_add_i32(arg[2].out, arg[2].in, arg[3].in);
}

void HELPER(mov_qacc_s3)(CPUXtensaState *env, uint32_t data_type, uint32_t qr_index)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if(ldqa_u8 == (ldqa_type)data_type)
    {
        ACCQ_reg *qacc_l = cpu_qacc_ptr(env, 0);
        ACCQ_reg *qacc_h = cpu_qacc_ptr(env, 1);
        QACC_reg q_reg_l;
        QACC_reg q_reg_h;
        for (int i=0 ; i< 8 ; i++)
        {
            q_reg_l.u20[i] = (uint32_t)tie->Q[qr_index].u8[i]; 
            q_reg_h.u20[i] = (uint32_t)tie->Q[qr_index].u8[i+8]; 
        }
        save_qacc20(&q_reg_l, qacc_l->u8);
        save_qacc20(&q_reg_h, qacc_h->u8);
    } else if(ldqa_s8 == (ldqa_type)data_type)
    {
        ACCQ_reg *qacc_l = cpu_qacc_ptr(env, 0);
        ACCQ_reg *qacc_h = cpu_qacc_ptr(env, 1);
        QACC_reg q_reg_l;
        QACC_reg q_reg_h;
        for (int i=0 ; i< 8 ; i++)
        {
            q_reg_l.s20[i] = (int32_t)tie->Q[qr_index].s8[i]; 
            q_reg_h.s20[i] = (int32_t)tie->Q[qr_index].s8[i+8]; 
        }
        save_qacc20(&q_reg_l, qacc_l->u8);
        save_qacc20(&q_reg_h, qacc_h->u8);
    } else if(ldqa_u16 == (ldqa_type)data_type)
    {
        ACCQ_reg *qacc_l = cpu_qacc_ptr(env, 0);
        ACCQ_reg *qacc_h = cpu_qacc_ptr(env, 1);
        QACC_reg q_reg_l;
        QACC_reg q_reg_h;
        for (int i=0 ; i< 4 ; i++)
        {
            q_reg_l.u40[i] = (uint64_t)tie->Q[qr_index].u16[i]; 
            q_reg_h.u40[i] = (uint64_t)tie->Q[qr_index].u16[i+4]; 
        }
        save_qacc40(&q_reg_l, qacc_l->u8);
        save_qacc40(&q_reg_h, qacc_h->u8);
    } else if(ldqa_s16 == (ldqa_type)data_type)
    {
        ACCQ_reg *qacc_l = cpu_qacc_ptr(env, 0);
        ACCQ_reg *qacc_h = cpu_qacc_ptr(env, 1);
        QACC_reg q_reg_l;
        QACC_reg q_reg_h;
        for (int i=0 ; i< 4 ; i++)
        {
            q_reg_l.s40[i] = (int64_t)tie->Q[qr_index].s16[i]; 
            q_reg_h.s40[i] = (int64_t)tie->Q[qr_index].s16[i+4]; 
        }
        save_qacc40(&q_reg_l, qacc_l->u8);
        save_qacc40(&q_reg_h, qacc_h->u8);
    }
    return;
}

static void translate_mov_qacc_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
        TCGv_i32 data_type = tcg_constant_i32((uint32_t)par[0]);
        TCGv_i32 qr_index  = tcg_constant_i32((uint32_t)arg[0].imm);

        gen_helper_mov_qacc_s3(tcg_env, data_type, qr_index);

        tcg_temp_free_i32(qr_index);
        tcg_temp_free_i32(data_type);

}

void HELPER(movi_q_s3)(CPUXtensaState *env, uint32_t qr_index, uint32_t sel, uint32_t data)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    tie->Q[qr_index].u32[sel] = data;
}

uint32_t HELPER(movi_a_s3)(CPUXtensaState *env, uint32_t qr_index, uint32_t sel)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint32_t result = tie->Q[qr_index].u32[sel];
    return result;
}

static void translate_movi_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qr_index  = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 sel  = tcg_constant_i32((uint32_t)arg[2].imm);

    if (ee_movi_a == (ee_movi_type)par[0])
    {
        gen_helper_movi_a_s3(arg[1].out, tcg_env, qr_index, sel);

    } else if (ee_movi_q == (ee_movi_type)par[0])
    {
        gen_helper_movi_q_s3(tcg_env, qr_index, sel, arg[1].in);
    }
    tcg_temp_free_i32(qr_index);
    tcg_temp_free_i32(sel);
}

void HELPER(vzip_s3)(CPUXtensaState *env, uint32_t qs0, uint32_t qs1, uint32_t width)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    Q_reg q_copy[2];
    Q_reg* q0 =  &tie->Q[qs0];
    Q_reg* q1 =  &tie->Q[qs1];
    q_copy[0].u64[0] = q0->u64[0];
    q_copy[0].u64[1] = q0->u64[1];
    q_copy[1].u64[0] = q1->u64[0];
    q_copy[1].u64[1] = q1->u64[1];
    if (vldbc_8 == (vldbc_type)width)
    {
        for (int i=0 ; i< 8 ; i++)
        {
            q0->u8[i*2 + 0] = q_copy[0].u8[i];
            q0->u8[i*2 + 1] = q_copy[1].u8[i];

            q1->u8[i*2 + 0] = q_copy[0].u8[i + 8];
            q1->u8[i*2 + 1] = q_copy[1].u8[i + 8];
        }
    } else if (vldbc_16 == (vldbc_type)width)
    {
        for (int i=0 ; i< 4 ; i++)
        {
            q0->u16[i*2 + 0] = q_copy[0].u16[i];
            q0->u16[i*2 + 1] = q_copy[1].u16[i];

            q1->u16[i*2 + 0] = q_copy[0].u16[i + 4];
            q1->u16[i*2 + 1] = q_copy[1].u16[i + 4];
        }

    } else if (vldbc_32 == (vldbc_type)width)
    {
        for (int i=0 ; i< 2 ; i++)
        {
            q0->u32[i*2 + 0] = q_copy[0].u32[i];
            q0->u32[i*2 + 1] = q_copy[1].u32[i];

            q1->u32[i*2 + 0] = q_copy[0].u32[i + 2];
            q1->u32[i*2 + 1] = q_copy[1].u32[i + 2];
        }
    }
}

void HELPER(vunzip_s3)(CPUXtensaState *env, uint32_t qs0, uint32_t qs1, uint32_t width)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    Q_reg q_copy[2];
    Q_reg* q0 =  &tie->Q[qs0];
    Q_reg* q1 =  &tie->Q[qs1];
    q_copy[0].u64[0] = q0->u64[0];
    q_copy[0].u64[1] = q0->u64[1];
    q_copy[1].u64[0] = q1->u64[0];
    q_copy[1].u64[1] = q1->u64[1];
    if (vldbc_8 == (vldbc_type)width)
    {
        for (int i=0 ; i< 8 ; i++)
        {
            q0->u8[i]     = q_copy[0].u8[i*2 + 0];
            q0->u8[i + 8] = q_copy[1].u8[i*2 + 0];

            q1->u8[i]     = q_copy[0].u8[i*2 + 1];
            q1->u8[i + 8] = q_copy[1].u8[i*2 + 1];
        }
    } else if (vldbc_16 == (vldbc_type)width)
    {
        for (int i=0 ; i< 4 ; i++)
        {
            q0->u16[i]     = q_copy[0].u16[i*2 + 0];
            q0->u16[i + 4] = q_copy[1].u16[i*2 + 0];

            q1->u16[i]     = q_copy[0].u16[i*2 + 1];
            q1->u16[i + 4] = q_copy[1].u16[i*2 + 1];
        }
    } else if (vldbc_32 == (vldbc_type)width)
    {
        for (int i=0 ; i< 2 ; i++)
        {
            q0->u32[i]     = q_copy[0].u32[i*2 + 0];
            q0->u32[i + 2] = q_copy[1].u32[i*2 + 0];

            q1->u32[i]     = q_copy[0].u32[i*2 + 1];
            q1->u32[i + 2] = q_copy[1].u32[i*2 + 1];
        }
    }
}

static void translate_zip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qs0  = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qs1  = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 width  = tcg_constant_i32((uint32_t)par[1]);
    if (0 == par[0]) // zip operation
    {
        gen_helper_vzip_s3(tcg_env, qs0, qs1, width);
    } else // unzip operation
    {
        gen_helper_vunzip_s3(tcg_env, qs0, qs1, width);
    }
    tcg_temp_free_i32(qs0);
    tcg_temp_free_i32(qs1);
    tcg_temp_free_i32(width);
}


void HELPER(vadds_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (ldqa_s8 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            int16_t result = (int16_t)tie->Q[qx].s8[i] + (int16_t)tie->Q[qy].s8[i];
            if (result > 0x7f) result = 0x7f;
            if (result < -0x7f) result = -0x7f;
            tie->Q[qz].s8[i] = result;
        }
    } else if (ldqa_s16 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            int32_t result = (int32_t)tie->Q[qx].s16[i] + (int32_t)tie->Q[qy].s16[i];
            if (result > 0x7fff) result = 0x7fff;
            if (result < -0x7fff) result = -0x7fff;
            tie->Q[qz].s16[i] = result;
        }
    } else if (ldqa_s32 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            int64_t result = (int64_t)tie->Q[qx].s32[i] + (int64_t)tie->Q[qy].s32[i];
            if (result > 0x7fffffff) result = 0x7fffffff;
            if (result < -0x7fffffff) result = -0x7fffffff;
            tie->Q[qz].s32[i] = result;
        }
    } else if (ldqa_u8 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            uint16_t result = (uint16_t)tie->Q[qx].u8[i] + (uint16_t)tie->Q[qy].u8[i];
            if (result > 0xff) result = 0xff;
            tie->Q[qz].u8[i] = result;
        }
    } else if (ldqa_u16 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            uint32_t result = (uint32_t)tie->Q[qx].u16[i] + (uint32_t)tie->Q[qy].u16[i];
            if (result > 0xffff) result = 0xffff;
            tie->Q[qz].u16[i] = result;
        }
    } else if (ldqa_u32 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            uint64_t result = (uint64_t)tie->Q[qx].u32[i] + (uint64_t)tie->Q[qy].u32[i];
            if (result > 0xffffffff) result = 0xffffffff;
            tie->Q[qz].u32[i] = result;
        }
    }
}

void HELPER(vsubs_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (ldqa_s8 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            int16_t result = (int16_t)tie->Q[qx].s8[i] - (int16_t)tie->Q[qy].s8[i];
            if (result > 0x7f) result = 0x7f;
            if (result < -0x7f) result = -0x7f;
            tie->Q[qz].s8[i] = result;
        }
    } else if (ldqa_s16 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            int32_t result = (int32_t)tie->Q[qx].s16[i] - (int32_t)tie->Q[qy].s16[i];
            if (result > 0x7fff) result = 0x7fff;
            if (result < -0x7fff) result = -0x7fff;
            tie->Q[qz].s16[i] = result;
        }
    } else if (ldqa_s32 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            int64_t result = (int64_t)tie->Q[qx].s32[i] - (int64_t)tie->Q[qy].s32[i];
            if (result > 0x7fffffff) result = 0x7fffffff;
            if (result < -0x7fffffff) result = -0x7fffffff;
            tie->Q[qz].s32[i] = result;
        }
    } else if (ldqa_u8 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            uint16_t result = (uint16_t)tie->Q[qx].u8[i] - (uint16_t)tie->Q[qy].u8[i];
            if (result > 0xff) result = 0xff;
            tie->Q[qz].u8[i] = result;
        }
    } else if (ldqa_u16 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            uint32_t result = (uint32_t)tie->Q[qx].u16[i] - (uint32_t)tie->Q[qy].u16[i];
            if (result > 0xffff) result = 0xffff;
            tie->Q[qz].u16[i] = result;
        }
    } else if (ldqa_u32 == (ldqa_type)op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            uint64_t result = (uint64_t)tie->Q[qx].u32[i] - (uint64_t)tie->Q[qy].u32[i];
            if (result > 0xffffffff) result = 0xffffffff;
            tie->Q[qz].u32[i] = result;
        }
    }
}

static void store_qreg_to_memory(DisasContext *dc, const OpcodeArg arg[])
{
    for (int i = 0; i < 2; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_vst_64_s3(data, tcg_env, index, low_high);
        tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
        tcg_temp_free_i32(low_high);
    }
}

static void load_qreg_from_memory(DisasContext *dc, const OpcodeArg arg[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 index;
        index = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_vld_64_s3(tcg_env, index, data, low_high);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
        tcg_temp_free_i32(low_high);
    }    
}

// par[0..2] = ldqa_s8, addr_nop, ee_load_op
// Addr_Update
// ee_ldst_type
static void translate_vadds_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    // First, we have to store the register to the memory
    int start_index = 0;
    if (addr_inc16 == (Addr_Update)par[1])
    {
        start_index = 2;
        if (ee_store_op == (ee_ldst_type)par[2])
        {
            store_qreg_to_memory(dc, arg);
        }
    }
    TCGv_i32 qz  = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qx  = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 qy  = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);
    TCGv_i32 op_type  = tcg_constant_i32((uint32_t)par[0]);

    if (ee_add_op == (ee_arithmetic_type)par[3])
    {
        gen_helper_vadds_s3(tcg_env, qz, qx, qy, op_type);
    } else if (ee_sub_op == (ee_arithmetic_type)par[3])
    {
        gen_helper_vsubs_s3(tcg_env, qz, qx, qy, op_type);
    }

    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    
    if (addr_inc16 == (Addr_Update)par[1])
    {
        if (ee_load_op == (ee_ldst_type)par[2])
        {
            // Store 
            load_qreg_from_memory(dc, arg);
        }
        tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
    }
}

void HELPER(vmul_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t sar, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (vmul_s8 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 16; i++)
        {
            int16_t result = ((int16_t)tie->Q[qx].s8[i] * (int16_t)tie->Q[qy].s8[i]) >> sar;
            tie->Q[qz].s8[i] = (int8_t)result;
        }        
    } else if (vmul_u8 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 16; i++)
        {
            uint16_t result = ((uint16_t)tie->Q[qx].u8[i] * (uint16_t)tie->Q[qy].u8[i]) >> sar;
            tie->Q[qz].u8[i] = (uint8_t)result;
        }
    } else if (vmul_s16 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 8; i++)
        {
            int32_t result = ((int32_t)tie->Q[qx].s16[i] * (int32_t)tie->Q[qy].s16[i]) >> sar;
            tie->Q[qz].s16[i] = (int16_t)result;
        }        
    } else if (vmul_u16 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 8; i++)
        {
            uint32_t result = ((uint32_t)tie->Q[qx].u16[i] * (uint32_t)tie->Q[qy].u16[i]) >> sar;
            tie->Q[qz].u16[i] = (uint32_t)result;
        }
    }
}

static void translate_vmul_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    int start_index = 0;

    if (addr_inc16 == (Addr_Update)par[1])
    {
        start_index = 2;
        if (ee_store_op == (ee_ldst_type)par[2])
        {
            store_qreg_to_memory(dc, arg);
        }
    }
    TCGv_i32 qz  = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qx  = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 qy  = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);
    TCGv_i32 op_type  = tcg_constant_i32((uint32_t)par[0]);
    if (dc->sar_m32_5bit)
    {
        gen_helper_vmul_s3(tcg_env, qz, qx, qy, dc->sar_m32, op_type);
    } else 
    {
        gen_helper_vmul_s3(tcg_env, qz, qx, qy, cpu_SR[SAR], op_type);
    }
    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);

    if (addr_inc16 == (Addr_Update)par[1])
    {
        if (ee_load_op == (ee_ldst_type)par[2])
        {
            // Store 
            load_qreg_from_memory(dc, arg);
        }
        tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
    }
}

void HELPER(cmul_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t sar, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int32_t result = 0;
    if (0 == op_type)
    {
        result = (((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[0]) - ((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[1])) >> sar;
        tie->Q[qz].s16[0] = result;
        result = (((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[1]) + ((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[0])) >> sar;
        tie->Q[qz].s16[1] = result;
        result = (((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[2]) - ((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[3])) >> sar;
        tie->Q[qz].s16[2] = result;
        result = (((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[3]) + ((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[2])) >> sar;
        tie->Q[qz].s16[3] = result;
    } else if (1 == op_type)
    {
        result = (((int32_t)tie->Q[qx].s16[0 + 4])*((int32_t)tie->Q[qy].s16[0 + 4]) - ((int32_t)tie->Q[qx].s16[1 + 4])*((int32_t)tie->Q[qy].s16[1 + 4])) >> sar;
        tie->Q[qz].s16[0 + 4] = result;
        result = (((int32_t)tie->Q[qx].s16[0 + 4])*((int32_t)tie->Q[qy].s16[1 + 4]) + ((int32_t)tie->Q[qx].s16[1 + 4])*((int32_t)tie->Q[qy].s16[0 + 4])) >> sar;
        tie->Q[qz].s16[1 + 4] = result;
        result = (((int32_t)tie->Q[qx].s16[2 + 4])*((int32_t)tie->Q[qy].s16[2 + 4]) - ((int32_t)tie->Q[qx].s16[3 + 4])*((int32_t)tie->Q[qy].s16[3 + 4])) >> sar;
        tie->Q[qz].s16[2 + 4] = result;
        result = (((int32_t)tie->Q[qx].s16[2 + 4])*((int32_t)tie->Q[qy].s16[3 + 4]) + ((int32_t)tie->Q[qx].s16[3 + 4])*((int32_t)tie->Q[qy].s16[2 + 4])) >> sar;
        tie->Q[qz].s16[3 + 4] = result;        
    } else if (2 == op_type)
    {
        result = (((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[0]) + ((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[1])) >> sar;
        tie->Q[qz].s16[0] = result;
        result = (((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[1]) - ((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[0])) >> sar;
        tie->Q[qz].s16[1] = result;
        result = (((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[2]) + ((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[3])) >> sar;
        tie->Q[qz].s16[2] = result;
        result = (((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[3]) - ((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[2])) >> sar;
        tie->Q[qz].s16[3] = result;
        
    } else if (3 == op_type)
    {
        result = (((int32_t)tie->Q[qx].s16[0 + 4])*((int32_t)tie->Q[qy].s16[0 + 4]) + ((int32_t)tie->Q[qx].s16[1 + 4])*((int32_t)tie->Q[qy].s16[1 + 4])) >> sar;
        tie->Q[qz].s16[0 + 4] = result;
        result = (((int32_t)tie->Q[qx].s16[0 + 4])*((int32_t)tie->Q[qy].s16[1 + 4]) - ((int32_t)tie->Q[qx].s16[1 + 4])*((int32_t)tie->Q[qy].s16[0 + 4])) >> sar;
        tie->Q[qz].s16[1 + 4] = result;
        result = (((int32_t)tie->Q[qx].s16[2 + 4])*((int32_t)tie->Q[qy].s16[2 + 4]) + ((int32_t)tie->Q[qx].s16[3 + 4])*((int32_t)tie->Q[qy].s16[3 + 4])) >> sar;
        tie->Q[qz].s16[2 + 4] = result;
        result = (((int32_t)tie->Q[qx].s16[2 + 4])*((int32_t)tie->Q[qy].s16[3 + 4]) - ((int32_t)tie->Q[qx].s16[3 + 4])*((int32_t)tie->Q[qy].s16[2 + 4])) >> sar;
        tie->Q[qz].s16[3 + 4] = result;                
    }
}

static void translate_cmul_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    if ((addr_inc16 == (Addr_Update)par[0]) && (ee_store_op == (ee_ldst_type)par[1]))
    {
        store_qreg_to_memory(dc, arg);
    }
    int start_index = 0;
    if (addr_nop == (Addr_Update)par[0])
    {
        start_index = 0;
    } else 
    {
        start_index = 2;
    }
    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)arg[start_index + 3].imm);
    if (dc->sar_m32_5bit)
    {
        gen_helper_cmul_s3(tcg_env, qz, qx, qy, dc->sar_m32, op_type);
    }
    else
    {
        gen_helper_cmul_s3(tcg_env, qz, qx, qy, cpu_SR[SAR], op_type);
    }
    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    if ((addr_inc16 == (Addr_Update)par[0]) && (ee_load_op == (ee_ldst_type)par[1]))
    {
        // Store 
        load_qreg_from_memory(dc, arg);
    }
    if (addr_inc16 == (Addr_Update)par[0])
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
    }
}

void HELPER(vmulas_accx_s3)(CPUXtensaState *env, uint32_t qx, uint32_t qy, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (vmul_s8 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 16; i++)
        {
            tie->ACCX += (int16_t)tie->Q[qx].s8[i] * (int16_t)tie->Q[qy].s8[i];
        }
        if (tie->ACCX > 0x7fffffffff)
        {
            tie->ACCX = 0x7fffffffff;
        }
        if (tie->ACCX < -0x7fffffffff)
        {
            tie->ACCX = -0x7fffffffff;
        }

    } else if (vmul_u8 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 16; i++)
        {
            tie->ACCX += (uint16_t)tie->Q[qx].u8[i] * (uint16_t)tie->Q[qy].u8[i];
        }
        if (tie->ACCX > 0xffffffffff)
        {
            tie->ACCX = 0xffffffffff;
        }
        if (tie->ACCX < 0)
        {
            tie->ACCX = 0;
        }
    } else if (vmul_s16 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 8; i++)
        {
            tie->ACCX += (int32_t)tie->Q[qx].s16[i] * (int32_t)tie->Q[qy].s16[i];
        }        
        if (tie->ACCX > 0x7fffffffff)
        {
            tie->ACCX = 0x7fffffffff;
        }
        if (tie->ACCX < -0x7fffffffff)
        {
            tie->ACCX = -0x7fffffffff;
        }
    } else if (vmul_u16 == (vmul_type)op_type)
    {
        for (size_t i = 0; i < 8; i++)
        {
            tie->ACCX += (uint32_t)tie->Q[qx].u16[i] * (uint32_t)tie->Q[qy].u16[i];
        }        
        if (tie->ACCX > 0xffffffffff)
        {
            tie->ACCX = 0xffffffffff;
        }
        if (tie->ACCX < 0)
        {
            tie->ACCX = 0;
        }
    }
}

static void translate_vmulas_accx_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    int start_index = 0;
    if (addr_nop == (Addr_Update)par[1])
    {
        start_index = 0;
    } else 
    {
        start_index = 3;
    }
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    gen_helper_vmulas_accx_s3(tcg_env, qx, qy, op_type);
    
    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    
    if (addr_nop != (Addr_Update)par[1])
    {
        load_qreg_from_memory(dc, arg);
    }
    if (addr_ip == (Addr_Update)par[1])
    {
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    }
    if (addr_xp == (Addr_Update)par[1])
    {
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    }

    if (par[2] == vmul_qup)
    {
        TCGv_i32 qs0 = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);
        TCGv_i32 qs1 = tcg_constant_i32((uint32_t)arg[start_index + 3].imm);

        gen_helper_vmulas_qup_s3(tcg_env, qs0, qs1);
        
        tcg_temp_free_i32(qs0);
        tcg_temp_free_i32(qs1);
    }
}

void HELPER(vmulas_qacc_s3)(CPUXtensaState *env, uint32_t qx, uint32_t qy, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    QACC_reg q_req_l;
    QACC_reg q_req_h;
    load_qacc(tie->ACCQ[0].u8, &q_req_l);
    load_qacc(tie->ACCQ[1].u8, &q_req_h);

    if (vmul_s8 == (vmul_type)op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            q_req_l.s20[i] += (int32_t)tie->Q[qx].s8[i]*(int32_t)tie->Q[qy].s8[i];
            if (q_req_l.s20[i] > 0x7ffff) q_req_l.s20[i] = 0x7ffff;
            if (q_req_l.s20[i] < -0x7ffff) q_req_l.s20[i] = -0x7ffff;
            q_req_h.s20[i] += (int32_t)tie->Q[qx].s8[i + 8]*(int32_t)tie->Q[qy].s8[i + 8];
            if (q_req_h.s20[i] > 0x7ffff) q_req_h.s20[i] = 0x7ffff;
            if (q_req_h.s20[i] < -0x7ffff) q_req_h.s20[i] = -0x7ffff;
        }
        save_qacc20(&q_req_l, tie->ACCQ[0].u8);
        save_qacc20(&q_req_h, tie->ACCQ[1].u8);
    } else if (vmul_u8 == (vmul_type)op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            q_req_l.u20[i] += (uint32_t)tie->Q[qx].u8[i]*(uint32_t)tie->Q[qy].u8[i];
            if (q_req_l.u20[i] > 0xfffff) q_req_l.u20[i] = -0xfffff;
            q_req_h.u20[i] += (uint32_t)tie->Q[qx].u8[i + 8]*(uint32_t)tie->Q[qy].u8[i + 8];
            if (q_req_h.u20[i] > 0xfffff) q_req_h.u20[i] = -0xfffff;
        }
        save_qacc20(&q_req_l, tie->ACCQ[0].u8);
        save_qacc20(&q_req_h, tie->ACCQ[1].u8);
    } else if (vmul_s16 == (vmul_type)op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            q_req_l.s40[i] += (int64_t)tie->Q[qx].s16[i]*(int64_t)tie->Q[qy].s16[i];
            if (q_req_l.s40[i] > 0x7fffffffff) q_req_l.s40[i] = 0x7fffffffff;
            if (q_req_l.s40[i] < -0x7fffffffff) q_req_l.s40[i] = -0x7fffffffff;
            q_req_h.s40[i] += (int64_t)tie->Q[qx].s16[i + 4]*(int64_t)tie->Q[qy].s16[i + 4];
            if (q_req_h.s40[i] > 0x7fffffffff) q_req_h.s40[i] = 0x7fffffffff;
            if (q_req_h.s40[i] < -0x7fffffffff) q_req_h.s40[i] = -0x7fffffffff;
        }
        save_qacc40(&q_req_l, tie->ACCQ[0].u8);
        save_qacc40(&q_req_h, tie->ACCQ[1].u8);
    } else if (vmul_u16 == (vmul_type)op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            q_req_l.u40[i] += (int64_t)tie->Q[qx].u16[i]*(int64_t)tie->Q[qy].u16[i];
            if (q_req_l.u40[i] > 0xffffffffff) q_req_l.u40[i] = 0xffffffffff;
            q_req_h.u40[i] += (int64_t)tie->Q[qx].u16[i + 4]*(int64_t)tie->Q[qy].u16[i + 4];
            if (q_req_h.u40[i] > 0xffffffffff) q_req_h.u40[i] = 0xffffffffff;
        }
        save_qacc40(&q_req_l, tie->ACCQ[0].u8);
        save_qacc40(&q_req_h, tie->ACCQ[1].u8);
    }
}

void HELPER(vmulas_qup_s3)(CPUXtensaState *env, uint32_t qs0, uint32_t qs1)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int shift = tie->SAR_BYTE;
    for (int i=0 ; i< (16-shift) ; i++)
    {
        tie->Q[qs0].u8[i] = tie->Q[qs0].u8[i + shift]; 
    }
    for (int i= 16-shift ; i< 16 ; i++)
    {
        tie->Q[qs0].u8[i] = tie->Q[qs1].u8[i - (16 - shift)];
    }
}

static void translate_vmulas_qacc_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    int start_index = 0;
    if (addr_nop == (Addr_Update)par[1])
    {
        start_index = 0;
    } else if (addr_ldbc_inc1 == (Addr_Update)par[1]) 
    {
        start_index = 2;   
    }else 
    {
        start_index = 3;
    }
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    gen_helper_vmulas_qacc_s3(tcg_env, qx, qy, op_type);
    
    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    if (addr_ldbc_inc1 == (Addr_Update)par[1])
    {
        if ((par[0] == vmul_s8) || (par[0] == vmul_u8))
        {
            const uint32_t* par_ldbc = (const uint32_t[]){addr_ldbc_inc1, vldbc_8};
            translate_vldbc_s3(dc, arg, par_ldbc);
        }
        if ((par[0] == vmul_s16) || (par[0] == vmul_u16))
        {
            const uint32_t* par_ldbc = (const uint32_t[]){addr_ldbc_inc1, vldbc_16};
            translate_vldbc_s3(dc, arg, par_ldbc);
        }
    }
    if (addr_ip == (Addr_Update)par[1])
    {
        load_qreg_from_memory(dc, arg);
        tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
    }
    if (addr_xp == (Addr_Update)par[1])
    {
        load_qreg_from_memory(dc, arg);
        tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
    }
    if (par[2] == vmul_qup)
    {
        TCGv_i32 qs0 = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);
        TCGv_i32 qs1 = tcg_constant_i32((uint32_t)arg[start_index + 3].imm);

        gen_helper_vmulas_qup_s3(tcg_env, qs0, qs1);
        
        tcg_temp_free_i32(qs0);
        tcg_temp_free_i32(qs1);
    }
}

void HELPER(vsmulas_s3)(CPUXtensaState *env, uint32_t qx, uint32_t qy, uint32_t op_type, uint32_t sel)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    QACC_reg q_req_l;
    QACC_reg q_req_h;
    load_qacc(tie->ACCQ[0].u8, &q_req_l);
    load_qacc(tie->ACCQ[1].u8, &q_req_h);

    if (op_type == vmul_s8)
    {
        int32_t tmp_s = tie->Q[qy].s8[sel];
        for (int i=0 ; i< 8 ; i++)
        {
            q_req_l.s20[i] += (int32_t)tie->Q[qx].s8[i] * tmp_s;
            if (q_req_l.s20[i] > 0x7ffff) q_req_l.s20[i] = 0x7ffff;
            if (q_req_l.s20[i] < -0x7ffff) q_req_l.s20[i] = -0x7ffff;
            
            q_req_h.s20[i] += (int32_t)tie->Q[qx].s8[i + 8] * tmp_s;
            if (q_req_h.s20[i] > 0x7ffff) q_req_h.s20[i] = 0x7ffff;
            if (q_req_h.s20[i] < -0x7ffff) q_req_h.s20[i] = -0x7ffff;
        }

        save_qacc20(&q_req_l, tie->ACCQ[0].u8);
        save_qacc20(&q_req_h, tie->ACCQ[1].u8);
    } else if (op_type == vmul_s16)
    {
        int32_t tmp_s = tie->Q[qy].s16[sel];
        for (int i=0 ; i< 4 ; i++)
        {
            q_req_l.s40[i] += (int32_t)tie->Q[qx].s16[i] * tmp_s;
            if (q_req_l.s40[i] > 0x7fffffffff) q_req_l.s40[i] = 0x7fffffffff;
            if (q_req_l.s40[i] < -0x7fffffffff) q_req_l.s40[i] = -0x7fffffffff;
            
            q_req_h.s40[i] += (int32_t)tie->Q[qx].s16[i + 4] * tmp_s;
            if (q_req_h.s40[i] > 0x7fffffffff) q_req_h.s40[i] = 0x7fffffffff;
            if (q_req_h.s40[i] < -0x7fffffffff) q_req_h.s40[i] = -0x7fffffffff;
        }

        save_qacc40(&q_req_l, tie->ACCQ[0].u8);
        save_qacc40(&q_req_h, tie->ACCQ[1].u8);
    }
}


static void translate_vsmulas_qacc_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    int start_index = 0;
    if (addr_nop == (Addr_Update)par[1])
    {
        start_index = 0;
    }
    if (addr_inc16 == (Addr_Update)par[1]) 
    {
        start_index = 2;   
    }

    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    TCGv_i32 op_sel = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);

    gen_helper_vsmulas_s3(tcg_env, qx, qy, op_type, op_sel);
    
    tcg_temp_free_i32(op_sel);
    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);

    if (addr_inc16 == (Addr_Update)par[1])
    {
        load_qreg_from_memory(dc, arg);
        tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
    }
}

void HELPER(srcmb_qacc_s3)(CPUXtensaState *env, uint32_t qu, uint32_t a0, uint32_t op_type, uint32_t sel)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    QACC_reg q_req_l;
    QACC_reg q_req_h;
    load_qacc(tie->ACCQ[0].u8, &q_req_l);
    load_qacc(tie->ACCQ[1].u8, &q_req_h);
    if (vmul_s8 == op_type)
    {
        for (int i=0 ; i< 8 ; i++)
        {
            q_req_l.s20[i] = q_req_l.s20[i] >> a0;
            q_req_h.s20[i] = q_req_h.s20[i] >> a0;
            tie->Q[qu].s8[i] = q_req_l.s20[i];
            tie->Q[qu].s8[i + 8] = q_req_h.s20[i];
            if (q_req_l.s20[i] > 0x7f)
            {
                tie->Q[qu].s8[i] = 0x7f;
            }
            if (q_req_l.s20[i] < -0x7f)
            {
                tie->Q[qu].s8[i] = 0x80;
            }
            if (q_req_h.s20[i] > 0x7f)
            {
                tie->Q[qu].s8[i + 8] = 0x7f;
            }
            if (q_req_h.s20[i] < -0x7f)
            {
                tie->Q[qu].s8[i + 8] = 0x80;
            }
        }
        save_qacc20(&q_req_l, tie->ACCQ[0].u8);
        save_qacc20(&q_req_h, tie->ACCQ[1].u8);
    } else if (vmul_s16 == op_type)
    {
        for (int i=0 ; i< 4 ; i++)
        {
            q_req_l.s40[i] = q_req_l.s40[i] >> a0;
            q_req_h.s40[i] = q_req_h.s40[i] >> a0;
            tie->Q[qu].s16[i] = q_req_l.s40[i];
            tie->Q[qu].s16[i + 4] = q_req_h.s40[i];
            if (q_req_l.s40[i] > 0x7fff)
            {
                tie->Q[qu].s16[i] = 0x7fff;
            }
            if (q_req_l.s40[i] < -0x7fff)
            {
                tie->Q[qu].s16[i] = 0x8000;
            }
            if (q_req_h.s40[i] > 0x7fff)
            {
                tie->Q[qu].s16[i + 4] = 0x7fff;
            }
            if (q_req_h.s40[i] < -0x7fff)
            {
                tie->Q[qu].s16[i + 4] = 0x8000;
            }
        }
        save_qacc40(&q_req_l, tie->ACCQ[0].u8);
        save_qacc40(&q_req_h, tie->ACCQ[1].u8);
    }
}

static void translate_srcmb_qacc_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qu = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    TCGv_i32 op_sel = tcg_constant_i32((uint32_t)arg[2].imm);

    gen_helper_srcmb_qacc_s3(tcg_env, qu, arg[1].in, op_type, op_sel);
    
    tcg_temp_free_i32(qu);
    tcg_temp_free_i32(op_sel);
    tcg_temp_free_i32(op_type);

}

void HELPER(vrelu_s3)(CPUXtensaState *env, uint32_t qs, uint32_t ax, uint32_t ay, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int32_t* pax = (int32_t*)&ax;
    if (vmul_s8 == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            if (tie->Q[qs].s8[i] <= 0)
            {
                int16_t m_result = ((int16_t)tie->Q[qs].s8[i]*(int16_t)(*pax))>>ay;
                tie->Q[qs].s8[i] = (int8_t)m_result;
            } else
            {
                tie->Q[qs].s8[i] = tie->Q[qs].s8[i];
            }
        }
    } else if (vmul_s16 == op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            if (tie->Q[qs].s16[i] <= 0)
            {
                int32_t m_result = ((int32_t)tie->Q[qs].s16[i]*(*pax))>>ay;
                tie->Q[qs].s16[i] = (int16_t)m_result;
            } else
            {
                tie->Q[qs].s16[i] = tie->Q[qs].s16[i];
            }
        }
    }

}

static void translate_vrelu_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qs = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    gen_helper_vrelu_s3(tcg_env, qs, arg[1].in, arg[2].in, op_type);
    
    tcg_temp_free_i32(qs);
    tcg_temp_free_i32(op_type);

}

void HELPER(vprelu_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t ay, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (vmul_s8 == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            if (tie->Q[qx].s8[i] <= 0)
            {
                int16_t m_result = ((int16_t)tie->Q[qx].s8[i]*(int16_t)tie->Q[qy].s8[i])>>ay;
                tie->Q[qz].s8[i] = (int8_t)m_result;
            } else
            {
                tie->Q[qz].s8[i] = tie->Q[qx].s8[i];
            }
        }
    } else if (vmul_s16 == op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            if (tie->Q[qx].s16[i] <= 0)
            {
                int32_t m_result = ((int32_t)tie->Q[qx].s16[i]*(int32_t)tie->Q[qy].s16[i])>>ay;
                tie->Q[qz].s16[i] = (int16_t)m_result;
            } else
            {
                tie->Q[qz].s16[i] = tie->Q[qx].s16[i];
            }
        }
    }
}

static void translate_vprelu_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    gen_helper_vprelu_s3(tcg_env, qz, qx, qy, arg[3].in, op_type);
    
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(op_type);

}

void HELPER(vmax_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (vmul_s8 == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            if (tie->Q[qx].s8[i] >= tie->Q[qy].s8[i])
            {
                tie->Q[qz].s8[i] = tie->Q[qx].s8[i];
            } else 
            {
                tie->Q[qz].s8[i] = tie->Q[qy].s8[i];
            }
        }
    } else if (vmul_s16 == op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            if (tie->Q[qx].s16[i] >= tie->Q[qy].s16[i])
            {
                tie->Q[qz].s16[i] = tie->Q[qx].s16[i];
            } else 
            {
                tie->Q[qz].s16[i] = tie->Q[qy].s16[i];
            }
        }
    } else if (vmul_s32 == op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            if (tie->Q[qx].s32[i] >= tie->Q[qy].s32[i])
            {
                tie->Q[qz].s32[i] = tie->Q[qx].s32[i];
            } else 
            {
                tie->Q[qz].s32[i] = tie->Q[qy].s32[i];
            }
        }
    }
}


static void translate_vmax_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    int start_index = 0;
    if (addr_inc16 == par[1])
    {
        start_index = 2;
        if (ee_store_op == (ee_ldst_type)par[2])
        {
            store_qreg_to_memory(dc, arg);
        }
    }

    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    gen_helper_vmax_s3(tcg_env, qz, qx, qy, op_type);
    
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(op_type);


    if (addr_inc16 == (Addr_Update)par[1])
    {
        if (ee_load_op == (ee_ldst_type)par[2])
        {
            // Load
            load_qreg_from_memory(dc, arg);
        }
        tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
    }
}

void HELPER(vmin_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (vmul_s8 == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            if (tie->Q[qx].s8[i] <= tie->Q[qy].s8[i])
            {
                tie->Q[qz].s8[i] = tie->Q[qx].s8[i];
            } else 
            {
                tie->Q[qz].s8[i] = tie->Q[qy].s8[i];
            }
        }
    } else if (vmul_s16 == op_type)
    {
        for (int i = 0; i < 8; i++)
        {
            if (tie->Q[qx].s16[i] <= tie->Q[qy].s16[i])
            {
                tie->Q[qz].s16[i] = tie->Q[qx].s16[i];
            } else 
            {
                tie->Q[qz].s16[i] = tie->Q[qy].s16[i];
            }
        }
    } else if (vmul_s32 == op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            if (tie->Q[qx].s32[i] <= tie->Q[qy].s32[i])
            {
                tie->Q[qz].s32[i] = tie->Q[qx].s32[i];
            } else 
            {
                tie->Q[qz].s32[i] = tie->Q[qy].s32[i];
            }
        }
    }
}


static void translate_vmin_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    int start_index = 0;
    if (addr_inc16 == par[1])
    {
        start_index = 2;
        if (ee_store_op == (ee_ldst_type)par[2])
        {
            store_qreg_to_memory(dc, arg);
        }
    }

    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[start_index + 0].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[start_index + 1].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[start_index + 2].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    gen_helper_vmin_s3(tcg_env, qz, qx, qy, op_type);
    
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(op_type);


    if (addr_inc16 == (Addr_Update)par[1])
    {
        if (ee_load_op == (ee_ldst_type)par[2])
        {
            // Load
            load_qreg_from_memory(dc, arg);
        }
        tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
    }
}

void HELPER(vcmp_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t op_type, uint32_t op_sel)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (ee_vcmp_eq == op_sel)
    {
        if (vmul_s8 == op_type)
        {
            for (int i = 0; i < 16; i++)
            {
                if (tie->Q[qx].s8[i] == tie->Q[qy].s8[i])
                {
                    tie->Q[qz].u8[i] = 0xff;
                } else 
                {
                    tie->Q[qz].u8[i] = 0;
                }
            }
        } else if (vmul_s16 == op_type)
        {
            for (int i = 0; i < 8; i++)
            {
                if (tie->Q[qx].s16[i] == tie->Q[qy].s16[i])
                {
                    tie->Q[qz].u16[i] = 0xffff;
                } else 
                {
                    tie->Q[qz].u16[i] = 0;
                }
            }
        } else if (vmul_s32 == op_type)
        {
            for (int i = 0; i < 4; i++)
            {
                if (tie->Q[qx].s32[i] == tie->Q[qy].s32[i])
                {
                    tie->Q[qz].u32[i] = 0xffffffff;
                } else 
                {
                    tie->Q[qz].u32[i] = 0;
                }
            }
        }
    } else if (ee_vcmp_lt == op_sel)
    {
        if (vmul_s8 == op_type)
        {
            for (int i = 0; i < 16; i++)
            {
                if (tie->Q[qx].s8[i] < tie->Q[qy].s8[i])
                {
                    tie->Q[qz].u8[i] = 0xff;
                } else 
                {
                    tie->Q[qz].u8[i] = 0;
                }
            }
        } else if (vmul_s16 == op_type)
        {
            for (int i = 0; i < 8; i++)
            {
                if (tie->Q[qx].s16[i] < tie->Q[qy].s16[i])
                {
                    tie->Q[qz].u16[i] = 0xffff;
                } else 
                {
                    tie->Q[qz].u16[i] = 0;
                }
            }
        } else if (vmul_s32 == op_type)
        {
            for (int i = 0; i < 4; i++)
            {
                if (tie->Q[qx].s32[i] < tie->Q[qy].s32[i])
                {
                    tie->Q[qz].u32[i] = 0xffffffff;
                } else 
                {
                    tie->Q[qz].u32[i] = 0;
                }
            }
        }
    } else if (ee_vcmp_gt == op_sel)
    {
        if (vmul_s8 == op_type)
        {
            for (int i = 0; i < 16; i++)
            {
                if (tie->Q[qx].s8[i] > tie->Q[qy].s8[i])
                {
                    tie->Q[qz].u8[i] = 0xff;
                } else 
                {
                    tie->Q[qz].u8[i] = 0;
                }
            }
        } else if (vmul_s16 == op_type)
        {
            for (int i = 0; i < 8; i++)
            {
                if (tie->Q[qx].s16[i] > tie->Q[qy].s16[i])
                {
                    tie->Q[qz].u16[i] = 0xffff;
                } else 
                {
                    tie->Q[qz].u16[i] = 0;
                }
            }
        } else if (vmul_s32 == op_type)
        {
            for (int i = 0; i < 4; i++)
            {
                if (tie->Q[qx].s32[i] > tie->Q[qy].s32[i])
                {
                    tie->Q[qz].u32[i] = 0xffffffff;
                } else 
                {
                    tie->Q[qz].u32[i] = 0;
                }
            }
        }
    }
}

static void translate_vcmp_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{

    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[2].imm);

    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    TCGv_i32 op_sel = tcg_constant_i32((uint32_t)par[1]);

    gen_helper_vcmp_s3(tcg_env, qz, qx, qy, op_type, op_sel);
    
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(op_sel);

}

void HELPER(bw_logic_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (bw_logic_or == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            tie->Q[qz].u8[i] = tie->Q[qx].u8[i]|tie->Q[qy].u8[i];
        }
    }
    else if (bw_logic_and == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            tie->Q[qz].u8[i] = tie->Q[qx].u8[i]&tie->Q[qy].u8[i];
        }
    } else if (bw_logic_xor == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            tie->Q[qz].u8[i] = tie->Q[qx].u8[i]^tie->Q[qy].u8[i];
        }        
    } else if (bw_logic_not == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            tie->Q[qz].u8[i] = ~tie->Q[qx].u8[i];
        }                
    }
}

static void translate_bw_logic_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    int y_index = 2;
    if (par[0] == bw_logic_not)
    {
        y_index = 1;
    }

    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[y_index].imm);

    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    gen_helper_bw_logic_s3(tcg_env, qz, qx, qy, op_type);
    
    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(op_type);
}

void HELPER(sxci_2q_s3)(CPUXtensaState *env, uint32_t qs0, uint32_t qs1, uint32_t sar, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int shift = sar + 1;
    uint8_t temp_arr[32];
    for (int i = 0; i < 16; i++)
    {
        temp_arr[i] = tie->Q[qs0].u8[i];
        temp_arr[i+16] = tie->Q[qs1].u8[i];
    }
    if (bw_shift_left == op_type)
    {
        for (int i = 0; i < shift; i++)
        {
            tie->Q[qs0].u8[i] = 0;
        }
        for (int i = shift; i < 16; i++)
        {
            tie->Q[qs0].u8[i] = temp_arr[i - shift];
        }
        for (int i = 0; i < 16; i++)
        {
            tie->Q[qs1].u8[i] = temp_arr[16 - shift + i];
        }
    } else if (bw_shift_right == op_type)
    {
        for (int i = 0; i < 16; i++)
        {
            tie->Q[qs0].u8[i] = temp_arr[i + shift];;
        }
        for (int i = 0; i < (16 - shift); i++)
        {
            tie->Q[qs1].u8[i] = temp_arr[i + shift + 16];
        }
        for (int i = (16 - shift); i < 16; i++)
        {
            tie->Q[qs1].u8[i] = 0;
        }
    }
}

static void translate_sxci_2q_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qs1 = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qs0 = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 sar = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    gen_helper_sxci_2q_s3(tcg_env, qs0, qs1, sar, op_type);


    tcg_temp_free_i32(qs0);
    tcg_temp_free_i32(qs1);
    tcg_temp_free_i32(sar);
    tcg_temp_free_i32(op_type);

}

static void translate_sxcxxp_2q_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qs1 = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qs0 = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    gen_helper_sxci_2q_s3(tcg_env, qs0, qs1, arg[2].in, op_type);

    tcg_temp_free_i32(qs0);
    tcg_temp_free_i32(qs1);
    tcg_temp_free_i32(op_type);

    tcg_gen_add_i32(arg[2].out, arg[2].in, arg[3].in);
}

uint64_t HELPER(srcq_64_rd_s3)(CPUXtensaState *env, uint32_t qs0, uint32_t qs1, uint32_t low_high)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = tie->Q[qs0].u64[low_high];

    int shift = tie->SAR_BYTE;
    uint8_t temp_arr[32];
    for (int i = 0; i < 16; i++)
    {
        temp_arr[i] = tie->Q[qs0].u8[i];
        temp_arr[i+16] = tie->Q[qs1].u8[i];
    }
    if (0 == low_high)
    {
        memcpy(&result, &temp_arr[shift], 8);

    } else if (1 == low_high)
    {
        memcpy(&result, &temp_arr[shift + 8], 8);
    } 
    return result;
}

static void translate_srcq_128_st_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[2].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory

        TCGv_i32 qs0 = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 qs1 = tcg_constant_i32((uint32_t)arg[1].imm);
        TCGv_i32 low_high = tcg_constant_i32(i);

        gen_helper_srcq_64_rd_s3(data, tcg_env, qs0, qs1, low_high);
        tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(qs0);
        tcg_temp_free_i32(qs1);
        tcg_temp_free_i32(low_high);
    }

    tcg_gen_addi_i32(arg[2].out, arg[2].in, 16);
}

void HELPER(vsx32_s3)(CPUXtensaState *env, uint32_t qa, uint32_t qs, uint32_t sar, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    if (bw_shift_right == op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            tie->Q[qa].s32[i] = tie->Q[qs].s32[i] >> sar;
        }        
    }    
    if (bw_shift_left == op_type)
    {
        for (int i = 0; i < 4; i++)
        {
            tie->Q[qa].s32[i] = tie->Q[qs].s32[i] << sar;
        }                
    }
}

static void translate_vsx32_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qa = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qs = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    if (dc->sar_m32_5bit)
    {
        gen_helper_vsx32_s3(tcg_env, qa, qs, dc->sar_m32, op_type);
    }
    else
    {
        gen_helper_vsx32_s3(tcg_env, qa, qs, cpu_SR[SAR], op_type);
    }

    tcg_temp_free_i32(qa);
    tcg_temp_free_i32(qs);
    tcg_temp_free_i32(op_type);
}

void HELPER(src_q_s3)(CPUXtensaState *env, uint32_t qa, uint32_t qs0, uint32_t qs1, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int sar_value = tie->SAR_BYTE;

    /* Need a temp copy since qa may alias qs0 or qs1 */
    Q_reg tmp;
    for (int i = 0; i < (16 - sar_value); i++)
    {
        tmp.u8[i] = tie->Q[qs0].u8[i + sar_value];
    }
    for (int i = 0; i < sar_value; i++)
    {
        tmp.u8[16 - sar_value + i] = tie->Q[qs1].u8[i];
    }
    tie->Q[qa].u64[0] = tmp.u64[0];
    tie->Q[qa].u64[1] = tmp.u64[1];

    /* QUP variant: also copy qs1 → qs0 */
    if (op_type == vmul_qup)
    {
        for (int i = 0; i < 16; i++)
        {
            tie->Q[qs0].u8[i] = tie->Q[qs1].u8[i];
        }
    }
}

static void translate_src_q_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    if (addr_nop == (Addr_Update)par[0])
    {
        TCGv_i32 qa = tcg_constant_i32((uint32_t)arg[0].imm);
        TCGv_i32 qs0 = tcg_constant_i32((uint32_t)arg[1].imm);
        TCGv_i32 qs1 = tcg_constant_i32((uint32_t)arg[2].imm);
        TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[1]);

        gen_helper_src_q_s3(tcg_env, qa, qs0, qs1, op_type);
        
        tcg_temp_free_i32(qa);
        tcg_temp_free_i32(qs0);
        tcg_temp_free_i32(qs1);
        tcg_temp_free_i32(op_type);

    } else 
    {
        TCGv_i32 qs0 = tcg_constant_i32((uint32_t)arg[3].imm);
        TCGv_i32 qs1 = tcg_constant_i32((uint32_t)arg[4].imm);

        gen_helper_vmulas_qup_s3(tcg_env, qs0, qs1);
        
        tcg_temp_free_i32(qs0);
        tcg_temp_free_i32(qs1);

        if (addr_ip == (Addr_Update)par[0])
        {
            load_qreg_from_memory(dc, arg);
            tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
        }
        if (addr_xp == (Addr_Update)par[0])
        {
            load_qreg_from_memory(dc, arg);
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

uint64_t HELPER(r2bf_st_low_s3)(CPUXtensaState *env, uint32_t qa0, uint32_t qx, uint32_t qy, uint32_t sar4)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* ptr_result = (int16_t*)&result;
    for (int i = 0; i < 8; i++)
    {
        tie->Q[qa0].s16[i] = tie->Q[qx].s16[i] - tie->Q[qy].s16[i];
    }
    for (int i=0; i< 4 ; i++)
    {
        int32_t temp = (tie->Q[qx].s16[i] + tie->Q[qy].s16[i]) >> sar4;
        ptr_result[i] = temp; 
    }
    return result;
}

uint64_t HELPER(r2bf_st_high_s3)(CPUXtensaState *env, uint32_t qa0, uint32_t qx, uint32_t qy, uint32_t sar4)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* ptr_result = (int16_t*)&result;

    for (int i=0; i< 4 ; i++)
    {
        int32_t temp = (tie->Q[qx].s16[i + 4] + tie->Q[qy].s16[i + 4]) >> sar4;
        ptr_result[i] = temp; 
    }
    return result;
}

static void translate_r2bf_st_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qa0 = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 sar4 = tcg_constant_i32((uint32_t)arg[4].imm);

    TCGv_i32 addr_low;
    TCGv_i32 addr_high;
    MemOp mop;

    addr_low = tcg_temp_new_i32();
    addr_high = tcg_temp_new_i32();
    // We have to align to 128 bit memory
    // Load low addr
    tcg_gen_andi_i32(addr_low, arg[3].in, 0xfffffff0);
    tcg_gen_addi_i32(addr_low, addr_low, 0);
    // Load high addr
    tcg_gen_andi_i32(addr_high, arg[3].in, 0xfffffff0);
    tcg_gen_addi_i32(addr_high, addr_high, 8);

    TCGv_i64 data_low = tcg_temp_new_i64();
    TCGv_i64 data_high = tcg_temp_new_i64();

    gen_helper_r2bf_st_low_s3(data_low, tcg_env, qa0, qx, qy, sar4);
    mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_low);
    tcg_gen_qemu_st_i64(data_low, addr_low, dc->cring, mop);

    gen_helper_r2bf_st_high_s3(data_high, tcg_env, qa0, qx, qy, sar4);
    mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_high);
    tcg_gen_qemu_st_i64(data_high, addr_high, dc->cring, mop);

    tcg_gen_addi_i32(arg[3].out, arg[3].in, 16);

    tcg_temp_free_i64(data_low);
    tcg_temp_free_i64(data_high);
    tcg_temp_free_i32(addr_low);
    tcg_temp_free_i32(addr_high);
    tcg_temp_free_i32(qa0);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(sar4);

}

void HELPER(r2bf_s3)(CPUXtensaState *env, uint32_t qa0, uint32_t qa1, uint32_t qx, uint32_t qy, uint32_t sel2)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int16_t op_a[8];
    int16_t op_b[8];
    for (int i = 0; i < 8; i++)
    {
        op_a[i] = 0;
        op_b[i] = 0;
    }
    
    if (sel2 == 0)
    {
        op_a[0] = tie->Q[qx].s16[0];
        op_a[1] = tie->Q[qx].s16[1];
        op_a[2] = tie->Q[qx].s16[2];
        op_a[3] = tie->Q[qx].s16[3];
        op_a[4] = tie->Q[qy].s16[0];
        op_a[5] = tie->Q[qy].s16[1];
        op_a[6] = tie->Q[qy].s16[2];
        op_a[7] = tie->Q[qy].s16[3];

        op_b[0] = tie->Q[qx].s16[4];
        op_b[1] = tie->Q[qx].s16[5];
        op_b[2] = tie->Q[qx].s16[6];
        op_b[3] = tie->Q[qx].s16[7];
        op_b[4] = tie->Q[qy].s16[4];
        op_b[5] = tie->Q[qy].s16[5];
        op_b[6] = tie->Q[qy].s16[6];
        op_b[7] = tie->Q[qy].s16[7];
    }
    if (sel2 == 1)
    {
        op_a[0] = tie->Q[qx].s16[0];
        op_a[1] = tie->Q[qx].s16[1];
        op_a[2] = tie->Q[qx].s16[4];
        op_a[3] = tie->Q[qx].s16[5];
        op_a[4] = tie->Q[qy].s16[0];
        op_a[5] = tie->Q[qy].s16[1];
        op_a[6] = tie->Q[qy].s16[4];
        op_a[7] = tie->Q[qy].s16[5];

        op_b[0] = tie->Q[qx].s16[2];
        op_b[1] = tie->Q[qx].s16[3];
        op_b[2] = tie->Q[qx].s16[6];
        op_b[3] = tie->Q[qx].s16[7];
        op_b[4] = tie->Q[qy].s16[2];
        op_b[5] = tie->Q[qy].s16[3];
        op_b[6] = tie->Q[qy].s16[6];
        op_b[7] = tie->Q[qy].s16[7];
    }

    tie->Q[qa0].s16[0] = op_a[0] + op_b[0];
    tie->Q[qa0].s16[1] = op_a[1] + op_b[1];
    tie->Q[qa0].s16[2] = op_a[2] + op_b[2];
    tie->Q[qa0].s16[3] = op_a[3] + op_b[3];
    tie->Q[qa0].s16[4] = op_a[0] - op_b[0];
    tie->Q[qa0].s16[5] = op_a[1] - op_b[1];
    tie->Q[qa0].s16[6] = op_a[2] - op_b[2];
    tie->Q[qa0].s16[7] = op_a[3] - op_b[3];

    tie->Q[qa1].s16[0] = op_a[4] + op_b[4];
    tie->Q[qa1].s16[1] = op_a[5] + op_b[5];
    tie->Q[qa1].s16[2] = op_a[6] + op_b[6];
    tie->Q[qa1].s16[3] = op_a[7] + op_b[7];
    tie->Q[qa1].s16[4] = op_a[4] - op_b[4];
    tie->Q[qa1].s16[5] = op_a[5] - op_b[5];
    tie->Q[qa1].s16[6] = op_a[6] - op_b[6];
    tie->Q[qa1].s16[7] = op_a[7] - op_b[7];
}

static void translate_r2bf_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qa0 = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qa1 = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[3].imm);
    TCGv_i32 sel2 = tcg_constant_i32((uint32_t)arg[4].imm);

    gen_helper_r2bf_s3(tcg_env, qa0, qa1, qx, qy, sel2);

    tcg_temp_free_i32(qa0);
    tcg_temp_free_i32(qa1);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(sel2);
}

void HELPER(fft_cmul_ld_s3)(CPUXtensaState *env, uint32_t qz, uint32_t qx, uint32_t qy, uint32_t sel8, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int64_t temp; 
    if (0 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[0]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[1]);
        tie->Q[qz].s16[0] = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[0]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[1]);
        tie->Q[qz].s16[1] = temp >> sar;
    }
    if (1 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[0]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[1]);
        tie->Q[qz].s16[0] = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[0]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[1]);
        tie->Q[qz].s16[1] = temp >> sar;
    }
    if (2 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[2]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[3]);
        tie->Q[qz].s16[2] = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[2]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[3]);
        tie->Q[qz].s16[3] = temp >> sar;
    }
    if (3 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[2]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[3]);
        tie->Q[qz].s16[2] = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[2]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[3]);
        tie->Q[qz].s16[3] = temp >> sar;
    }
    if (4 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[4]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[5]);
        tie->Q[qz].s16[4] = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[4]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[5]);
        tie->Q[qz].s16[5] = temp >> sar;
    }
    if (5 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[4]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[5]);
        tie->Q[qz].s16[4] = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[4]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[5]);
        tie->Q[qz].s16[5] = temp >> sar;
    }
}

static void translate_fft_cmul_ld_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[3].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[4].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[5].imm);
    TCGv_i32 sel8 = tcg_constant_i32((uint32_t)arg[6].imm);

    if (dc->sar_m32_5bit)
    {
        gen_helper_fft_cmul_ld_s3(tcg_env, qz, qx, qy, sel8, dc->sar_m32);
    } else 
    {
        gen_helper_fft_cmul_ld_s3(tcg_env, qz, qx, qy, sel8, cpu_SR[SAR]);
    }

    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(sel8);

    // Update memory
    load_qreg_from_memory(dc, arg);

    tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
}

// EE.FFT.CMUL.S16.ST.XP	qx, qy, qv, as, ad, sel8, upd4, sar4, 
uint64_t HELPER(fft_cmul_st_low_s3)(CPUXtensaState *env, uint32_t qx, uint32_t qy, uint32_t qv, uint32_t sel8, uint32_t upd4, uint32_t sar4, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* prt_result = (int16_t*)&result;
    if (0 == upd4)
    {
        prt_result[0] = tie->Q[qv].s16[0];
        prt_result[1] = tie->Q[qv].s16[1];
        prt_result[2] = tie->Q[qv].s16[2];
        prt_result[3] = tie->Q[qv].s16[3];
    }
    if (1 == upd4)
    {
        prt_result[0] = tie->Q[qx].s16[0]>>sar4;
        prt_result[1] = tie->Q[qx].s16[1]>>sar4;
        prt_result[2] = tie->Q[qx].s16[2]>>sar4;
        prt_result[3] = tie->Q[qx].s16[3]>>sar4;
    }
    if (2 == upd4)
    {
        prt_result[0] = tie->Q[qx].s16[0]>>sar4;
        prt_result[1] = tie->Q[qx].s16[1]>>sar4;
        prt_result[2] = tie->Q[qv].s16[4];
        prt_result[3] = tie->Q[qv].s16[5];
    }
    return result;
}

uint64_t HELPER(fft_cmul_st_high_s3)(CPUXtensaState *env, uint32_t qx, uint32_t qy, uint32_t qv, uint32_t sel8, uint32_t upd4, uint32_t sar4, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* prt_result = (int16_t*)&result;
    int16_t  temp0 = 0;
    int16_t  temp1 = 0;
    int64_t  temp = 0;

    if (0 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[0]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[1]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[0]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[1]);
        temp1 = temp >> sar;
    }
    if (1 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[0]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[1]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[1])*((int32_t)tie->Q[qy].s16[0]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[0])*((int32_t)tie->Q[qy].s16[1]);
        temp1 = temp >> sar;
    }
    if (2 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[2]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[3]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[2]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[3]);
        temp1 = temp >> sar;
    }
    if (3 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[2]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[3]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[3])*((int32_t)tie->Q[qy].s16[2]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[2])*((int32_t)tie->Q[qy].s16[3]);
        temp1 = temp >> sar;
    }
    if (4 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[4]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[5]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[4]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[5]);
        temp1 = temp >> sar;
    }
    if (5 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[4]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[5]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[5])*((int32_t)tie->Q[qy].s16[4]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[4])*((int32_t)tie->Q[qy].s16[5]);
        temp1 = temp >> sar;
    }
    if (6 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[6])*((int32_t)tie->Q[qy].s16[6]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[7])*((int32_t)tie->Q[qy].s16[7]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[7])*((int32_t)tie->Q[qy].s16[6]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[6])*((int32_t)tie->Q[qy].s16[7]);
        temp1 = temp >> sar;
    }
    if (7 == sel8)
    {
        temp = (int64_t)((int32_t)tie->Q[qx].s16[6])*((int32_t)tie->Q[qy].s16[6]);
        temp -= (int64_t)((int32_t)tie->Q[qx].s16[7])*((int32_t)tie->Q[qy].s16[7]);
        temp0 = temp >> sar;
        temp = (int64_t)((int32_t)tie->Q[qx].s16[7])*((int32_t)tie->Q[qy].s16[6]);
        temp += (int64_t)((int32_t)tie->Q[qx].s16[6])*((int32_t)tie->Q[qy].s16[7]);
        temp1 = temp >> sar;
    }
    if (0 == upd4)
    {
        prt_result[0] = tie->Q[qv].s16[4];
        prt_result[1] = tie->Q[qv].s16[5];
        prt_result[2] = temp0;
        prt_result[3] = temp1;
    }
    if (1 == upd4)
    {
        prt_result[0] = tie->Q[qv].s16[4];
        prt_result[1] = tie->Q[qv].s16[5];
        prt_result[2] = temp0;
        prt_result[3] = temp1;
    }
    if (2 == upd4)
    {
        prt_result[0] = tie->Q[qx].s16[2]>>sar4;
        prt_result[1] = tie->Q[qx].s16[3]>>sar4;
        prt_result[2] = temp0;
        prt_result[3] = temp1;
    }
    return result;
}


static void translate_fft_cmul_st_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 qv = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 sel8 = tcg_constant_i32((uint32_t)arg[5].imm);
    TCGv_i32 upd4 = tcg_constant_i32((uint32_t)arg[6].imm);
    TCGv_i32 sar4 = tcg_constant_i32((uint32_t)arg[7].imm);

    TCGv_i32 addr_low;
    TCGv_i32 addr_high;
    MemOp mop;

    addr_low = tcg_temp_new_i32();
    addr_high = tcg_temp_new_i32();
    // We have to align to 128 bit memory
    // Load low addr
    tcg_gen_andi_i32(addr_low, arg[3].in, 0xfffffff0);
    tcg_gen_addi_i32(addr_low, addr_low, 0);
    // Load high addr
    tcg_gen_andi_i32(addr_high, arg[3].in, 0xfffffff0);
    tcg_gen_addi_i32(addr_high, addr_high, 8);

    TCGv_i64 data_low = tcg_temp_new_i64();
    TCGv_i64 data_high = tcg_temp_new_i64();

    if (dc->sar_m32_5bit)
    {
        gen_helper_fft_cmul_st_low_s3(data_low, tcg_env, qx, qy, qv, sel8, upd4, sar4, dc->sar_m32);
    } else 
    {
        gen_helper_fft_cmul_st_low_s3(data_low, tcg_env, qx, qy, qv, sel8, upd4, sar4, cpu_SR[SAR]);
    }

    mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_low);
    tcg_gen_qemu_st_i64(data_low, addr_low, dc->cring, mop);

    if (dc->sar_m32_5bit)
    {
        gen_helper_fft_cmul_st_high_s3(data_high, tcg_env, qx, qy, qv, sel8, upd4, sar4, dc->sar_m32);
    } else 
    {
        gen_helper_fft_cmul_st_high_s3(data_high, tcg_env, qx, qy, qv, sel8, upd4, sar4, cpu_SR[SAR]);
    }

    mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_high);
    tcg_gen_qemu_st_i64(data_high, addr_high, dc->cring, mop);

    tcg_temp_free_i64(data_low);
    tcg_temp_free_i64(data_high);
    tcg_temp_free_i32(addr_low);
    tcg_temp_free_i32(addr_high);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(qv);
    tcg_temp_free_i32(sar4);

    tcg_gen_add_i32(arg[3].out, arg[3].in, arg[4].in);

}

static uint16_t reverse(uint16_t x, int order)
{
    uint16_t result = 0;
    for (int i = 0; i < order; i++)
    {
        if (x&(1<<i))
        {
            result |= 1<<(order - i - 1);
        }
    }
    return result;
}

void HELPER(bitrev_s3)(CPUXtensaState *env, uint32_t qa, uint32_t as)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    for (int i = 0; i < 8; i++)
    {
        tie->Q[qa].u16[i] = as + i;
        int16_t rev = reverse( as + i, tie->fft_width);
        if (rev > tie->Q[qa].u16[i]) tie->Q[qa].u16[i] = rev; 
    }    
}

static void translate_bitrev_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qa = tcg_constant_i32((uint32_t)arg[0].imm);

    gen_helper_bitrev_s3(tcg_env, qa, arg[1].in);

    tcg_temp_free_i32(qa);
    tcg_gen_addi_i32(arg[1].out, arg[1].in, 8);
}

void HELPER(fft_ams_s16)(CPUXtensaState *env, uint32_t qz, uint32_t qz1, uint32_t qx, uint32_t qy, uint32_t qm, uint32_t sel2, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int16_t temp0 = tie->Q[qx].s16[2] + tie->Q[qy].s16[2];
    int16_t temp1 = tie->Q[qx].s16[3] - tie->Q[qy].s16[3];
    int16_t temp2 = 0;
    int16_t temp3 = 0;
    if (0 == sel2)
    {
        int64_t temp = (int64_t)((int32_t)(tie->Q[qx].s16[2] - tie->Q[qy].s16[2])*(int32_t)tie->Q[qm].s16[2]);
        temp -= (int64_t)((int32_t)(tie->Q[qx].s16[3] + tie->Q[qy].s16[3])*(int32_t)tie->Q[qm].s16[3]);
        temp2 = temp >> sar;
        temp = (int64_t)((int32_t)(tie->Q[qx].s16[2] - tie->Q[qy].s16[2])*(int32_t)tie->Q[qm].s16[3]);
        temp += (int64_t)((int32_t)(tie->Q[qx].s16[3] + tie->Q[qy].s16[3])*(int32_t)tie->Q[qm].s16[2]);
        temp3 = temp >> sar;
    }
    if (1 == sel2)
    {
        int64_t temp = (int64_t)((int32_t)(tie->Q[qx].s16[3] + tie->Q[qy].s16[3])*(int32_t)tie->Q[qm].s16[3]);
        temp += (int64_t)((int32_t)(tie->Q[qx].s16[2] - tie->Q[qy].s16[2])*(int32_t)tie->Q[qm].s16[2]);
        temp2 = temp >> sar;
        temp = (int64_t)((int32_t)(tie->Q[qx].s16[3] + tie->Q[qy].s16[3])*(int32_t)tie->Q[qm].s16[2]);
        temp -= (int64_t)((int32_t)(tie->Q[qx].s16[2] - tie->Q[qy].s16[2])*(int32_t)tie->Q[qm].s16[3]);
        temp3 = temp >> sar;
    }
    tie->Q[qz].s16[2] = temp0 + temp2;
    tie->Q[qz].s16[3] = temp1 + temp3;
    tie->Q[qz1].s16[2] = temp0 - temp2;
    tie->Q[qz1].s16[3] = temp3 - temp1;

}

static void translate_fft_ams_s16_ld_incp(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 qz1 = tcg_constant_i32((uint32_t)arg[3].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[4].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[5].imm);
    TCGv_i32 qm = tcg_constant_i32((uint32_t)arg[6].imm);
    TCGv_i32 sel2 = tcg_constant_i32((uint32_t)arg[7].imm);

    if (dc->sar_m32_5bit)
    {
        gen_helper_fft_ams_s16(tcg_env, qz, qz1, qx, qy, qm, sel2, dc->sar_m32);
    } else 
    {
        gen_helper_fft_ams_s16(tcg_env, qz, qz1, qx, qy, qm, sel2, cpu_SR[SAR]);
    }


    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qz1);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(qm);
    tcg_temp_free_i32(sel2);

    // Load from memory
    load_qreg_from_memory(dc, arg);
    // incp
    tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
}

uint64_t HELPER(fft_ams_st_0_s16_low)(CPUXtensaState *env, uint32_t qv, uint32_t qz1, uint32_t as0, uint32_t qx, uint32_t qy, uint32_t qm, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* ptr_result = (int16_t*)&result;
    int16_t* ptr_as = (int16_t*)&as0;

    ptr_result[0] =ptr_as[0]>>1;
    ptr_result[1] =ptr_as[1]>>1;

    ptr_result[2] = tie->Q[qv].s16[0] >> 1;
    ptr_result[3] = tie->Q[qv].s16[1] >> 1;

    return result;
}

uint64_t HELPER(fft_ams_st_0_s16_high)(CPUXtensaState *env, uint32_t qv, uint32_t qz1, uint32_t as0, uint32_t qx, uint32_t qy, uint32_t qm, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* ptr_result = (int16_t*)&result;

    ptr_result[0] = tie->Q[qv].s16[2] >> 1;
    ptr_result[1] = tie->Q[qv].s16[3] >> 1;
    ptr_result[2] = tie->Q[qv].s16[4] >> 1;
    ptr_result[3] = tie->Q[qv].s16[5] >> 1;

    int16_t temp0 = tie->Q[qx].s16[6] + tie->Q[qy].s16[6];
    int16_t temp1 = tie->Q[qx].s16[7] - tie->Q[qy].s16[7];

    int16_t temp2 = 0;
    int16_t temp3 = 0;
    int16_t temp4 = 0;
    int16_t temp5 = 0;
    int64_t temp;

    temp = (int64_t)((int32_t)(tie->Q[qx].s16[6] - tie->Q[qy].s16[6])*(int32_t)tie->Q[qm].s16[6]);
    temp -= (int64_t)((int32_t)(tie->Q[qx].s16[7] + tie->Q[qy].s16[7])*(int32_t)tie->Q[qm].s16[7]);
    temp2 = temp >> sar;

    temp = (int64_t)((int32_t)(tie->Q[qx].s16[6] - tie->Q[qy].s16[6])*(int32_t)tie->Q[qm].s16[7]);
    temp += (int64_t)((int32_t)(tie->Q[qx].s16[7] + tie->Q[qy].s16[7])*(int32_t)tie->Q[qm].s16[6]);
    temp3 = temp >> sar;

    temp4 = temp1  + temp3;
    temp5 = temp0  + temp2;

    tie->Q[qz1].s16[6] = temp0 - temp2;
    tie->Q[qz1].s16[7] = temp3 - temp1;

    tie->temp_asm[1] = temp4;
    tie->temp_asm[0] = temp5;
    return result;
}

uint64_t HELPER(fft_ams_st_1_s16_low)(CPUXtensaState *env, uint32_t qv, uint32_t qz1, uint32_t as0, uint32_t qx, uint32_t qy, uint32_t qm, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* ptr_result = (int16_t*)&result;
    int16_t* ptr_as = (int16_t*)&as0;

    ptr_result[0] =ptr_as[0];
    ptr_result[1] =ptr_as[1];

    ptr_result[2] = tie->Q[qv].s16[0];
    ptr_result[3] = tie->Q[qv].s16[1];

    return result;
}

uint64_t HELPER(fft_ams_st_1_s16_high)(CPUXtensaState *env, uint32_t qv, uint32_t qz1, uint32_t as0, uint32_t qx, uint32_t qy, uint32_t qm, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint64_t result = 0;
    int16_t* ptr_result = (int16_t*)&result;

    ptr_result[0] = tie->Q[qv].s16[2];
    ptr_result[1] = tie->Q[qv].s16[3];
    ptr_result[2] = tie->Q[qv].s16[4];
    ptr_result[3] = tie->Q[qv].s16[5];

    int16_t temp0 = tie->Q[qx].s16[6] + tie->Q[qy].s16[6];
    int16_t temp1 = tie->Q[qx].s16[7] - tie->Q[qy].s16[7];

    int16_t temp2 = 0;
    int16_t temp3 = 0;
    int16_t temp4 = 0;
    int16_t temp5 = 0;
    int64_t temp;

    temp = (int64_t)((int32_t)(tie->Q[qx].s16[7] + tie->Q[qy].s16[7])*(int32_t)tie->Q[qm].s16[7]);
    temp += (int64_t)((int32_t)(tie->Q[qx].s16[6] - tie->Q[qy].s16[6])*(int32_t)tie->Q[qm].s16[6]);
    temp2 = temp >> sar;

    temp = (int64_t)((int32_t)(tie->Q[qx].s16[7] + tie->Q[qy].s16[7])*(int32_t)tie->Q[qm].s16[6]);
    temp -= (int64_t)((int32_t)(tie->Q[qx].s16[6] - tie->Q[qy].s16[6])*(int32_t)tie->Q[qm].s16[7]);
    temp3 = temp >> sar;

    temp4 = temp1  + temp3;
    temp5 = temp0  + temp2;

    tie->Q[qz1].s16[6] = temp0 - temp2;
    tie->Q[qz1].s16[7] = temp3 - temp1;

    tie->temp_asm[1] = temp4;
    tie->temp_asm[0] = temp5;

    return result;
}

uint32_t HELPER(fft_ams_st_s16_at)(CPUXtensaState *env)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint32_t result = 0;
    memcpy(&result, tie->temp_asm, sizeof(uint32_t));
    return result;
}

static void translate_fft_ams_s16_st_incp(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qv = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 qz1 = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[4].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[5].imm);
    TCGv_i32 qm = tcg_constant_i32((uint32_t)arg[6].imm);
    
    TCGv_i64 data_low = tcg_temp_new_i64();
    TCGv_i64 data_high = tcg_temp_new_i64();

    if ((uint32_t)arg[7].imm == 0)
    {
        if (dc->sar_m32_5bit)
        {
            gen_helper_fft_ams_st_0_s16_low(data_low, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, dc->sar_m32);
            gen_helper_fft_ams_st_0_s16_high(data_high, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, dc->sar_m32);
        } else 
        {
            gen_helper_fft_ams_st_0_s16_low(data_low, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, cpu_SR[SAR]);
            gen_helper_fft_ams_st_0_s16_high(data_high, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, cpu_SR[SAR]);
        }
    } else 
    {
        if (dc->sar_m32_5bit)
        {
            gen_helper_fft_ams_st_1_s16_low(data_low, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, dc->sar_m32);
            gen_helper_fft_ams_st_1_s16_high(data_high, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, dc->sar_m32);
        } else 
        {
            gen_helper_fft_ams_st_1_s16_low(data_low, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, cpu_SR[SAR]);
            gen_helper_fft_ams_st_1_s16_high(data_high, tcg_env, qv, qz1, arg[2].in, qx, qy, qm, cpu_SR[SAR]);
        }
    }

    tcg_temp_free_i32(qv);
    tcg_temp_free_i32(qz1);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(qm);

    TCGv_i32 addr_low;
    TCGv_i32 addr_high;
    MemOp mop;

    addr_low = tcg_temp_new_i32();
    addr_high = tcg_temp_new_i32();
    // We have to align to 128 bit memory
    // Load low addr
    tcg_gen_andi_i32(addr_low, arg[3].in, 0xfffffff0);
    tcg_gen_addi_i32(addr_low, addr_low, 0);
    // Load high addr
    tcg_gen_andi_i32(addr_high, arg[3].in, 0xfffffff0);
    tcg_gen_addi_i32(addr_high, addr_high, 8);

    mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_low);
    tcg_gen_qemu_st_i64(data_low, addr_low, dc->cring, mop);

    mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_high);
    tcg_gen_qemu_st_i64(data_high, addr_high, dc->cring, mop);

    tcg_temp_free_i64(data_low);
    tcg_temp_free_i64(data_high);

    // Load from memory
    TCGv_i32 data_as = tcg_temp_new_i32();

    gen_helper_fft_ams_st_s16_at(data_as, tcg_env);
    tcg_gen_mov_i32(arg[2].out, data_as);

    tcg_temp_free_i32(data_as);
    // incp
    tcg_gen_addi_i32(arg[3].out, arg[3].in, 16);
}

void HELPER(fft_ams_s16_uqup)(CPUXtensaState *env, uint32_t qz, uint32_t qz1, uint32_t qx, uint32_t qy, uint32_t qm, uint32_t sel2, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int16_t temp0 = tie->Q[qx].s16[0] + tie->Q[qy].s16[0];
    int16_t temp1 = tie->Q[qx].s16[1] - tie->Q[qy].s16[1];
    int16_t temp2 = 0;
    int16_t temp3 = 0;
    if (0 == sel2)
    {
        int64_t temp = (int64_t)((int32_t)(tie->Q[qx].s16[0] - tie->Q[qy].s16[0])*(int32_t)tie->Q[qm].s16[0]);
        temp -= (int64_t)((int32_t)(tie->Q[qx].s16[1] + tie->Q[qy].s16[1])*(int32_t)tie->Q[qm].s16[1]);
        temp2 = temp >> sar;
        temp = (int64_t)((int32_t)(tie->Q[qx].s16[0] - tie->Q[qy].s16[0])*(int32_t)tie->Q[qm].s16[1]);
        temp += (int64_t)((int32_t)(tie->Q[qx].s16[1] + tie->Q[qy].s16[1])*(int32_t)tie->Q[qm].s16[0]);
        temp3 = temp >> sar;
    }
    if (1 == sel2)
    {
        int64_t temp = (int64_t)((int32_t)(tie->Q[qx].s16[1] + tie->Q[qy].s16[1])*(int32_t)tie->Q[qm].s16[1]);
        temp += (int64_t)((int32_t)(tie->Q[qx].s16[0] - tie->Q[qy].s16[0])*(int32_t)tie->Q[qm].s16[0]);
        temp2 = temp >> sar;
        temp = (int64_t)((int32_t)(tie->Q[qx].s16[1] + tie->Q[qy].s16[1])*(int32_t)tie->Q[qm].s16[0]);
        temp -= (int64_t)((int32_t)(tie->Q[qx].s16[0] - tie->Q[qy].s16[0])*(int32_t)tie->Q[qm].s16[1]);
        temp3 = temp >> sar;
    }
    tie->Q[qz].s16[0] = temp0 + temp2;
    tie->Q[qz].s16[1] = temp1 + temp3;
    tie->Q[qz1].s16[0] = temp0 - temp2;
    tie->Q[qz1].s16[1] = temp3 - temp1;
}

void HELPER(fft_ams_s16_ld_incp_uaup)(CPUXtensaState *env, uint32_t qu, uint64_t data_low, uint64_t data_high)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint8_t inData[32];
    
    memcpy(&inData[0], tie->UA_STATE.u8, 16);
    memcpy(&inData[16], &data_low, 8);
    memcpy(&inData[16 + 8], &data_high, 8);
    int shift = tie->SAR_BYTE;
    for (int i=0 ; i< 16; i++)
    {
        tie->Q[qu].u8[i] = inData[i + shift];
    }
    memcpy(tie->UA_STATE.u8, &inData[16], 16);
}

static void translate_fft_ams_s16_ld_incp_uaup(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 qz1 = tcg_constant_i32((uint32_t)arg[3].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[4].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[5].imm);
    TCGv_i32 qm = tcg_constant_i32((uint32_t)arg[6].imm);
    TCGv_i32 sel2 = tcg_constant_i32((uint32_t)arg[7].imm);

    if (dc->sar_m32_5bit)
    {
        gen_helper_fft_ams_s16_uqup(tcg_env, qz, qz1, qx, qy, qm, sel2, dc->sar_m32);
    } else 
    {
        gen_helper_fft_ams_s16_uqup(tcg_env, qz, qz1, qx, qy, qm, sel2, cpu_SR[SAR]);
    }

    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qz1);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(qm);
    tcg_temp_free_i32(sel2);

    // Load q and ua_state from memory
    {
        TCGv_i32 addr_low = tcg_temp_new_i32();
        TCGv_i32 addr_high = tcg_temp_new_i32();
        MemOp mop_low;
        MemOp mop_high;
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr_low, arg[1].in, 0xfffffff0);
        tcg_gen_andi_i32(addr_high, arg[1].in, 0xfffffff0);
        tcg_gen_addi_i32(addr_high, addr_high, 8);

        mop_low = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_low);
        mop_high = gen_load_store_alignment(dc, MO_64 | MO_TE, addr_high);

        // Read data from memory
        TCGv_i64 data_low = tcg_temp_new_i64();
        tcg_gen_qemu_ld_i64(data_low, addr_low, dc->cring, mop_low);
        TCGv_i64 data_high = tcg_temp_new_i64();
        tcg_gen_qemu_ld_i64(data_high, addr_high, dc->cring, mop_high);

        TCGv_i32 qu = tcg_constant_i32((uint32_t)arg[0].imm);

        gen_helper_fft_ams_s16_ld_incp_uaup(tcg_env, qu, data_low, data_high);

        tcg_temp_free_i32(addr_low);
        tcg_temp_free_i32(addr_high);
        tcg_temp_free_i64(data_low);
        tcg_temp_free_i64(data_high);
        tcg_temp_free_i32(qu);
    }

    // incp
    tcg_gen_addi_i32(arg[1].out, arg[1].in, 16);
}


void HELPER(fft_ams_s16_decp)(CPUXtensaState *env, uint32_t qz, uint32_t qz1, uint32_t qx, uint32_t qy, uint32_t qm, uint32_t sel2, uint32_t sar)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int16_t temp0 = tie->Q[qx].s16[4] + tie->Q[qy].s16[4];
    int16_t temp1 = tie->Q[qx].s16[5] - tie->Q[qy].s16[5];
    int16_t temp2 = 0;
    int16_t temp3 = 0;
    if (0 == sel2)
    {
        int64_t temp = (int64_t)((int32_t)(tie->Q[qx].s16[4] - tie->Q[qy].s16[4])*(int32_t)tie->Q[qm].s16[4]);
        temp -= (int64_t)((int32_t)(tie->Q[qx].s16[5] + tie->Q[qy].s16[5])*(int32_t)tie->Q[qm].s16[5]);
        temp2 = temp >> sar;
        temp = (int64_t)((int32_t)(tie->Q[qx].s16[4] - tie->Q[qy].s16[4])*(int32_t)tie->Q[qm].s16[5]);
        temp += (int64_t)((int32_t)(tie->Q[qx].s16[5] + tie->Q[qy].s16[5])*(int32_t)tie->Q[qm].s16[4]);
        temp3 = temp >> sar;
    }
    if (1 == sel2)
    {
        int64_t temp = (int64_t)((int32_t)(tie->Q[qx].s16[5] + tie->Q[qy].s16[5])*(int32_t)tie->Q[qm].s16[5]);
        temp += (int64_t)((int32_t)(tie->Q[qx].s16[4] - tie->Q[qy].s16[4])*(int32_t)tie->Q[qm].s16[4]);
        temp2 = temp >> sar;
        temp = (int64_t)((int32_t)(tie->Q[qx].s16[5] + tie->Q[qy].s16[5])*(int32_t)tie->Q[qm].s16[4]);
        temp -= (int64_t)((int32_t)(tie->Q[qx].s16[4] - tie->Q[qy].s16[4])*(int32_t)tie->Q[qm].s16[5]);
        temp3 = temp >> sar;
    }
    tie->Q[qz].s16[4] = temp0 + temp2;
    tie->Q[qz].s16[5] = temp1 + temp3;
    tie->Q[qz1].s16[4] = temp0 - temp2;
    tie->Q[qz1].s16[5] = temp3 - temp1;

}
void HELPER(fft_ams_s16_exchange_q)(CPUXtensaState *env, uint32_t qu)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int32_t temp;
    temp = tie->Q[qu].s32[0]; 
    tie->Q[qu].s32[0] = tie->Q[qu].s32[3];
    tie->Q[qu].s32[3] = temp;
    temp = tie->Q[qu].s32[1]; 
    tie->Q[qu].s32[1] = tie->Q[qu].s32[2];
    tie->Q[qu].s32[2] = temp;
}

static void translate_fft_ams_s16_ld_decp(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 qz = tcg_constant_i32((uint32_t)arg[2].imm);
    TCGv_i32 qz1 = tcg_constant_i32((uint32_t)arg[3].imm);
    TCGv_i32 qx = tcg_constant_i32((uint32_t)arg[4].imm);
    TCGv_i32 qy = tcg_constant_i32((uint32_t)arg[5].imm);
    TCGv_i32 qm = tcg_constant_i32((uint32_t)arg[6].imm);
    TCGv_i32 sel2 = tcg_constant_i32((uint32_t)arg[7].imm);

    if (dc->sar_m32_5bit)
    {
        gen_helper_fft_ams_s16_decp(tcg_env, qz, qz1, qx, qy, qm, sel2, dc->sar_m32);
    } else 
    {
        gen_helper_fft_ams_s16_decp(tcg_env, qz, qz1, qx, qy, qm, sel2, cpu_SR[SAR]);
    }

    tcg_temp_free_i32(qz);
    tcg_temp_free_i32(qz1);
    tcg_temp_free_i32(qx);
    tcg_temp_free_i32(qy);
    tcg_temp_free_i32(qm);
    tcg_temp_free_i32(sel2);

    // Load from memory
    load_qreg_from_memory(dc, arg);
    TCGv_i32 qu = tcg_constant_i32((uint32_t)arg[0].imm);
    gen_helper_fft_ams_s16_exchange_q(tcg_env, qu);
    tcg_temp_free_i32(qu);

    // incp
    tcg_gen_subi_i32(arg[1].out, arg[1].in, 16);    
}

uint32_t HELPER(wr_mask_gpio_out_s3)(CPUXtensaState *env, uint32_t as, uint32_t ax, uint32_t op_type)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint32_t result = 0;
    if (gpio_mask == op_type)
    {
        tie->gpio_out = (tie->gpio_out &~ax) | (as & ax);
    }
    if (gpio_set == op_type)
    {
        tie->gpio_out = (tie->gpio_out | as);
    }
    if (gpio_clr == op_type)
    {
        tie->gpio_out = (tie->gpio_out & ~as);
    }
    if (gpio_in == op_type)
    {
        result = tie->gpio_out;
    }
    return result;
}


static void translate_wr_mask_gpio_out_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    TCGv_i32 data = tcg_temp_new_i32();

    if (gpio_mask == (uint32_t)par[0])
    {
        gen_helper_wr_mask_gpio_out_s3(data, tcg_env, arg[0].in, arg[1].in, op_type);
    } else if ((gpio_set == (uint32_t)par[0]) | (gpio_clr == (uint32_t)par[0]))
    {    
        TCGv_i32 as = tcg_constant_i32((uint32_t)arg[0].imm);
        gen_helper_wr_mask_gpio_out_s3(data, tcg_env, as, op_type, op_type);
        tcg_temp_free_i32(as);
    } else 
    {
        gen_helper_wr_mask_gpio_out_s3(data, tcg_env, op_type, op_type, op_type);
        tcg_gen_mov_i32(arg[0].out, data);
    }
    tcg_temp_free_i32(op_type);
    tcg_temp_free_i32(data);
}

void HELPER(ld_accx_s3)(CPUXtensaState *env, uint64_t data)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    tie->ACCX = data & 0x000000ffffffffff;
    return;
}

static void translate_ld_accx_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    // We have to align to 64 bit memory
    tcg_gen_andi_i32(addr, arg[0].in, 0xfffffff8);

    MemOp mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

    TCGv_i64 data = tcg_temp_new_i64();
    // Read data from memory
    tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

    gen_helper_ld_accx_s3(tcg_env, data);

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);

    tcg_temp_free_i64(data);
    tcg_temp_free_i32(addr);
}

uint32_t HELPER(srs_accx_s3)(CPUXtensaState *env, uint32_t shift)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    uint32_t result = 0;
    // tie->ACCX = (tie->ACCX&0x000000ffffffffff) >> shift;
    tie->ACCX = tie->ACCX >> shift;
    result = (uint32_t)tie->ACCX;
    // printf("srs_accx_s3 = %i\n", result);
    return result;
}

static void translate_srs_accx_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 shift = tcg_temp_new_i32();
    tcg_gen_andi_i32(shift, arg[1].in, 0x3f);
    gen_helper_srs_accx_s3(arg[0].out, tcg_env, shift);
    tcg_temp_free_i32(shift);
}


static void translate_ld_qacc_x_h_32_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    TCGv_i32 addr = tcg_temp_new_i32();
    // We have to align to 32 bit memory
    tcg_gen_andi_i32(addr, arg[0].in, 0xfffffffc);

    MemOp mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

    TCGv_i32 data = tcg_temp_new_i32();
    // Read data from memory
    tcg_gen_qemu_ld_i32(data, addr, dc->cring, mop);

    gen_helper_ld_qacc_x_h_32_ip_s3(tcg_env, data, op_type);

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);

    tcg_temp_free_i32(data);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(op_type);
}

void HELPER(ld_qacc_x_h_32_ip_s3)(CPUXtensaState *env, uint32_t data, uint32_t op_type)
{
    // printf("ld_qacc_x_h_32_ip_s3 data = %8.8x, type = %i\n", data, op_type);
    ACCQ_reg *qacc = cpu_qacc_ptr(env, 1);
    if (op_type == wrur_qacc_l_0)
    {
        qacc = cpu_qacc_ptr(env, 0);

    }
    uint8_t* data_ptr = (uint8_t*)&data;
    qacc->u8[16] = data_ptr[0];
    qacc->u8[17] = data_ptr[1];
    qacc->u8[18] = data_ptr[2];
    qacc->u8[19] = data_ptr[3];
}

void HELPER(ld_qacc_x_l_128_ip_s3)(CPUXtensaState *env, uint32_t op_type, uint32_t index, uint64_t data)
{
    ACCQ_reg *qacc = cpu_qacc_ptr(env, 1);
    if (op_type == wrur_qacc_l_0)
    {
        qacc = cpu_qacc_ptr(env, 0);

    }
    uint8_t* data_ptr = (uint8_t*)&data;
    for (int i=0 ; i< 8 ; i++)
    {
        qacc->u8[i + index*8] = data_ptr[i];
    }
}

/* Store upper 32 bits of QACC_H or QACC_L to memory */
uint32_t HELPER(st_qacc_x_h_32_ip_s3)(CPUXtensaState *env, uint32_t op_type)
{
    ACCQ_reg *qacc = cpu_qacc_ptr(env, 1);
    if (op_type == wrur_qacc_l_0)
    {
        qacc = cpu_qacc_ptr(env, 0);
    }
    uint32_t data;
    uint8_t* data_ptr = (uint8_t*)&data;
    data_ptr[0] = qacc->u8[16];
    data_ptr[1] = qacc->u8[17];
    data_ptr[2] = qacc->u8[18];
    data_ptr[3] = qacc->u8[19];
    return data;
}

/* Store lower 128 bits of QACC_H or QACC_L to memory (64-bit chunk) */
uint64_t HELPER(st_qacc_x_l_128_ip_s3)(CPUXtensaState *env, uint32_t op_type, uint32_t index)
{
    ACCQ_reg *qacc = cpu_qacc_ptr(env, 1);
    if (op_type == wrur_qacc_l_0)
    {
        qacc = cpu_qacc_ptr(env, 0);
    }
    uint64_t data;
    uint8_t* data_ptr = (uint8_t*)&data;
    for (int i = 0; i < 8; i++)
    {
        data_ptr[i] = qacc->u8[i + index * 8];
    }
    return data;
}

static void translate_st_qacc_x_h_32_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);
    TCGv_i32 addr = tcg_temp_new_i32();
    // Align to 32 bit
    tcg_gen_andi_i32(addr, arg[0].in, 0xfffffffc);

    MemOp mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

    TCGv_i32 data = tcg_temp_new_i32();
    gen_helper_st_qacc_x_h_32_ip_s3(data, tcg_env, op_type);

    tcg_gen_qemu_st_i32(data, addr, dc->cring, mop);

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);

    tcg_temp_free_i32(data);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(op_type);
}

static void translate_st_qacc_x_l_128_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    for (int i = 0; i < 2; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // Align to 128 bit
        tcg_gen_andi_i32(addr, arg[0].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        TCGv_i32 index = tcg_constant_i32(i);

        gen_helper_st_qacc_x_l_128_ip_s3(data, tcg_env, op_type, index);

        tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
    }

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);
    tcg_temp_free_i32(op_type);
}

static void translate_ld_qacc_x_l_128_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 op_type = tcg_constant_i32((uint32_t)par[0]);

    for (int i=0 ; i < 2 ; i++)
    {
        TCGv_i32 addr;
        MemOp mop;
        addr = tcg_temp_new_i32();
        // We have to align to 128 bit memory
        tcg_gen_andi_i32(addr, arg[0].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i*8);

        mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        // Read data from memory
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 index = tcg_constant_i32(i);

        gen_helper_ld_qacc_x_l_128_ip_s3(tcg_env, op_type, index, data);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(index);
    }

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);
    tcg_temp_free_i32(op_type);
}

/* EE.ST.ACCX.IP: store ACCX (40-bit, zero-extended to 64-bit) to memory */
uint64_t HELPER(st_accx_s3)(CPUXtensaState *env)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    return (uint64_t)(tie->ACCX & 0x000000ffffffffffULL);
}

static void translate_st_accx_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    /* Align to 8 bytes */
    tcg_gen_andi_i32(addr, arg[0].in, 0xfffffff8);

    MemOp mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

    TCGv_i64 data = tcg_temp_new_i64();
    gen_helper_st_accx_s3(data, tcg_env);

    tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);

    tcg_temp_free_i64(data);
    tcg_temp_free_i32(addr);
}

/* EE.LD.UA_STATE.IP: load 128-bit UA_STATE from memory */
void HELPER(ld_ua_state_s3)(CPUXtensaState *env, uint64_t data, uint32_t low_high)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    tie->UA_STATE.u64[low_high] = data;
}

static void translate_ld_ua_state_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i = 0; i < 2; i++)
    {
        TCGv_i32 addr = tcg_temp_new_i32();
        /* Align to 16 bytes */
        tcg_gen_andi_i32(addr, arg[0].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 8);

        MemOp mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        tcg_gen_qemu_ld_i64(data, addr, dc->cring, mop);

        TCGv_i32 low_high = tcg_constant_i32(i);
        gen_helper_ld_ua_state_s3(tcg_env, data, low_high);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(low_high);
    }

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);
}

/* EE.ST.UA_STATE.IP: store 128-bit UA_STATE to memory */
uint64_t HELPER(st_ua_state_s3)(CPUXtensaState *env, uint32_t low_high)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    return tie->UA_STATE.u64[low_high];
}

static void translate_st_ua_state_ip_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    for (int i = 0; i < 2; i++)
    {
        TCGv_i32 addr = tcg_temp_new_i32();
        /* Align to 16 bytes */
        tcg_gen_andi_i32(addr, arg[0].in, 0xfffffff0);
        tcg_gen_addi_i32(addr, addr, i * 8);

        MemOp mop = gen_load_store_alignment(dc, MO_64 | MO_TE, addr);

        TCGv_i64 data = tcg_temp_new_i64();
        TCGv_i32 low_high = tcg_constant_i32(i);
        gen_helper_st_ua_state_s3(data, tcg_env, low_high);

        tcg_gen_qemu_st_i64(data, addr, dc->cring, mop);

        tcg_temp_free_i64(data);
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(low_high);
    }

    tcg_gen_addi_i32(arg[0].out, arg[0].in, arg[1].imm);
}

/* EE.LDXQ.32: indexed load 32-bit into Q register segment */
void HELPER(ldxq_32_s3)(CPUXtensaState *env, uint32_t qu, uint32_t sel4, uint32_t data)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    tie->Q[qu].u32[sel4] = data;
}

static void translate_ldxq_32_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    /* EE.LDXQ.32 qu, qs, as, sel4, sel8 */
    /* arg[0]=qu, arg[1]=qs, arg[2]=as, arg[3]=sel4(imm), arg[4]=sel8(imm) */
    uint32_t sel8 = (uint32_t)arg[4].imm;
    uint32_t sel4 = (uint32_t)arg[3].imm;

    /* Compute address: as + sign_extend(qs.s16[sel8]) * 4 - 4, aligned to 4 */
    TCGv_i32 qs_idx = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 sel8_val = tcg_constant_i32(sel8);
    TCGv_i32 offset = tcg_temp_new_i32();
    gen_helper_ldstxq_addr_s3(offset, tcg_env, qs_idx, sel8_val);

    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[2].in, offset);
    tcg_gen_andi_i32(addr, addr, 0xfffffffc);

    MemOp mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);

    TCGv_i32 data = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(data, addr, dc->cring, mop);

    TCGv_i32 qu_idx = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 sel4_val = tcg_constant_i32(sel4);
    gen_helper_ldxq_32_s3(tcg_env, qu_idx, sel4_val, data);

    tcg_temp_free_i32(data);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(offset);
    tcg_temp_free_i32(qs_idx);
    tcg_temp_free_i32(sel8_val);
    tcg_temp_free_i32(qu_idx);
    tcg_temp_free_i32(sel4_val);
}

/* EE.STXQ.32: indexed store 32-bit from Q register segment */
uint32_t HELPER(stxq_32_s3)(CPUXtensaState *env, uint32_t qv, uint32_t sel4)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    return tie->Q[qv].u32[sel4];
}

/* Helper to compute address offset: sign_extend(qs.s16[sel8]) * 4 - 4 */
uint32_t HELPER(ldstxq_addr_s3)(CPUXtensaState *env, uint32_t qs, uint32_t sel8)
{
    CPUXtensaEsp32s3State* tie = (CPUXtensaEsp32s3State*)(env->ext);
    int32_t offset = (int32_t)tie->Q[qs].s16[sel8];
    return offset * 4 - 4;
}

static void translate_stxq_32_s3(DisasContext *dc, const OpcodeArg arg[], const uint32_t par[])
{
    /* EE.STXQ.32 qv, qs, as, sel4, sel8 */
    /* arg[0]=qv, arg[1]=qs, arg[2]=as, arg[3]=sel4(imm), arg[4]=sel8(imm) */
    uint32_t sel8 = (uint32_t)arg[4].imm;
    uint32_t sel4 = (uint32_t)arg[3].imm;

    /* Get data to store */
    TCGv_i32 qv_idx = tcg_constant_i32((uint32_t)arg[0].imm);
    TCGv_i32 sel4_val = tcg_constant_i32(sel4);
    TCGv_i32 data = tcg_temp_new_i32();
    gen_helper_stxq_32_s3(data, tcg_env, qv_idx, sel4_val);

    /* Compute address: as + sign_extend(qs.s16[sel8]) * 4 - 4, aligned to 4 */
    TCGv_i32 qs_idx = tcg_constant_i32((uint32_t)arg[1].imm);
    TCGv_i32 sel8_val = tcg_constant_i32(sel8);
    TCGv_i32 offset = tcg_temp_new_i32();
    gen_helper_ldstxq_addr_s3(offset, tcg_env, qs_idx, sel8_val);

    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_add_i32(addr, arg[2].in, offset);
    tcg_gen_andi_i32(addr, addr, 0xfffffffc);

    MemOp mop = gen_load_store_alignment(dc, MO_32 | MO_TE, addr);
    tcg_gen_qemu_st_i32(data, addr, dc->cring, mop);

    tcg_temp_free_i32(data);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(offset);
    tcg_temp_free_i32(qv_idx);
    tcg_temp_free_i32(sel4_val);
    tcg_temp_free_i32(qs_idx);
    tcg_temp_free_i32(sel8_val);
}

static const XtensaOpcodeOps tie_ops[] = {
    // VLD.L/H.64.ip/xp
    {
        .name = "ee.vld.l.64.ip",
        .translate = translate_vld_64_s3,
        .par = (const uint32_t[]){addr_ip, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vld.h.64.ip",
        .translate = translate_vld_64_s3,
        .par = (const uint32_t[]){addr_ip, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vld.l.64.xp",
        .translate = translate_vld_64_s3,
        .par = (const uint32_t[]){addr_xp, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vld.h.64.xp",
        .translate = translate_vld_64_s3,
        .par = (const uint32_t[]){addr_xp, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VLD 128 bit
    {
        .name = "ee.vld.128.ip",
        .translate = translate_vld_128_s3,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vld.128.xp",
        .translate = translate_vld_128_s3,
        .par = (const uint32_t[]){addr_xp},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // LD.USAR.128
    {
        .name = "ee.ld.128.usar.ip",
        .translate = translate_ld_usar_128_s3,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ld.128.usar.xp",
        .translate = translate_ld_usar_128_s3,
        .par = (const uint32_t[]){addr_xp},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // LDQA.[U/S].[8/16].[IP/XP]
    {
        .name = "ee.ldqa.u8.128.ip",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_ip, ldqa_u8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldqa.u16.128.ip",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_ip, ldqa_u16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldqa.s8.128.ip",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_ip, ldqa_s8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldqa.s16.128.ip",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_ip, ldqa_s16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldqa.u8.128.xp",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_xp, ldqa_u8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldqa.u16.128.xp",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_xp, ldqa_u16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldqa.s8.128.xp",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_xp, ldqa_s8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldqa.s16.128.xp",
        .translate = translate_ldqa_128_s3,
        .par = (const uint32_t[]){addr_xp, ldqa_s16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VLDBC 8/16/32 bit (bare, no address update)
    {
        .name = "ee.vldbc.8",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_nop, vldbc_8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vldbc.16",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_nop, vldbc_16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vldbc.32",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_nop, vldbc_32},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VLDBC 8/16/32 bit (with address post-update)
    {
        .name = "ee.vldbc.8.ip",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_ip, vldbc_8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vldbc.8.xp",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_xp, vldbc_8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vldbc.16.ip",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_ip, vldbc_16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vldbc.16.xp",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_xp, vldbc_16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vldbc.32.ip",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_ip, vldbc_32},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vldbc.32.xp",
        .translate = translate_vldbc_s3,
        .par = (const uint32_t[]){addr_xp, vldbc_32},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // vldhbc:
    {
        .name = "ee.vldhbc.16.incp",
        .translate = translate_vldhbc_s3,
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // store operations:
    {
        .name = "ee.vst.l.64.ip",
        .translate = translate_vst_64_s3,
        .par = (const uint32_t[]){addr_ip, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vst.h.64.ip",
        .translate = translate_vst_64_s3,
        .par = (const uint32_t[]){addr_ip, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vst.l.64.xp",
        .translate = translate_vst_64_s3,
        .par = (const uint32_t[]){addr_xp, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vst.h.64.xp",
        .translate = translate_vst_64_s3,
        .par = (const uint32_t[]){addr_xp, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vst.128.ip",
        .translate = translate_vst_128_s3,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vst.128.xp",
        .translate = translate_vst_128_s3,
        .par = (const uint32_t[]){addr_xp},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // LDF/STF operations:
    {
        .name = "ee.ldf.128.ip",
        .translate = translate_ldf_128_ip,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.stf.128.ip",
        .translate = translate_stf_128_ip,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldf.64.ip",
        .translate = translate_ldf_64_ip,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.stf.64.ip",
        .translate = translate_stf_64_ip,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldf.128.xp",
        .translate = translate_ldf_128_xp,
        .par = (const uint32_t[]){addr_xp},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.stf.128.xp",
        .translate = translate_stf_128_xp,
        .par = (const uint32_t[]){addr_xp},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ldf.64.xp",
        .translate = translate_ldf_64_xp,
        .par = (const uint32_t[]){addr_xp},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.stf.64.xp",
        .translate = translate_stf_64_xp,
        .par = (const uint32_t[]){addr_xp},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    // Set to zero or dump
    {
        .name = "ee.zero.qacc",
        .translate = translate_zero,
        .par = (const uint32_t[]){ee_zero_qacc},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.zero.accx",
        .translate = translate_zero,
        .par = (const uint32_t[]){ee_zero_accx},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.zero.q",
        .translate = translate_zero,
        .par = (const uint32_t[]){ee_zero_qx},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // WUR operations
    {
        .name = "wur.accx_0",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_accx_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.accx_1",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_accx_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_l_0",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_l_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_l_1",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_l_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_l_2",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_l_2},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_l_3",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_l_3},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_l_4",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_l_4},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_h_0",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_h_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_h_1",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_h_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_h_2",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_h_2},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_h_3",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_h_3},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.qacc_h_4",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_qacc_h_4},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.gpio_out",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_gpio_out},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.sar_byte",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_sar_byte},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.fft_bit_width",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_fft_bit_width},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.ua_state_0",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_ua_state_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.ua_state_1",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_ua_state_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.ua_state_2",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_ua_state_2},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "wur.ua_state_3",
        .translate = translate_wur,
        .par = (const uint32_t[]){wrur_ua_state_3},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // RUR operations
    {
        .name = "rur.accx_0",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_accx_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.accx_1",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_accx_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_l_0",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_l_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_l_1",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_l_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_l_2",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_l_2},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_l_3",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_l_3},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_l_4",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_l_4},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_h_0",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_h_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_h_1",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_h_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_h_2",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_h_2},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_h_3",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_h_3},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.qacc_h_4",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_qacc_h_4},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.gpio_out",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_gpio_out},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.sar_byte",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_sar_byte},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.fft_bit_width",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_fft_bit_width},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.ua_state_0",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_ua_state_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.ua_state_1",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_ua_state_1},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.ua_state_2",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_ua_state_2},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "rur.ua_state_3",
        .translate = translate_rur,
        .par = (const uint32_t[]){wrur_ua_state_3},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Data exchange:
    // ee.mov.[u/s][8/16].qacc  qx
    {
        .name = "ee.mov.u8.qacc",
        .translate = translate_mov_qacc_s3,
        .par = (const uint32_t[]){ldqa_u8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.mov.s8.qacc",
        .translate = translate_mov_qacc_s3,
        .par = (const uint32_t[]){ldqa_s8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.mov.u16.qacc",
        .translate = translate_mov_qacc_s3,
        .par = (const uint32_t[]){ldqa_u16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.mov.s16.qacc",
        .translate = translate_mov_qacc_s3,
        .par = (const uint32_t[]){ldqa_s16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // movi.a/q
    {
        .name = "ee.movi.32.a",
        .translate = translate_movi_s3,
        .par = (const uint32_t[]){ee_movi_a},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.movi.32.q",
        .translate = translate_movi_s3,
        .par = (const uint32_t[]){ee_movi_q},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // vzip/vunzip
    {
        .name = "ee.vzip.8",
        .translate = translate_zip_s3,
        .par = (const uint32_t[]){0, vldbc_8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vzip.16",
        .translate = translate_zip_s3,
        .par = (const uint32_t[]){0, vldbc_16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vzip.32",
        .translate = translate_zip_s3,
        .par = (const uint32_t[]){0, vldbc_32},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vunzip.8",
        .translate = translate_zip_s3,
        .par = (const uint32_t[]){1, vldbc_8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vunzip.16",
        .translate = translate_zip_s3,
        .par = (const uint32_t[]){1, vldbc_16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vunzip.32",
        .translate = translate_zip_s3,
        .par = (const uint32_t[]){1, vldbc_32},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Arithmetic instructions...
    // VADDS ...
    // VADDS.s[8/16/32] qz,qx,qy
    {
        .name = "ee.vadds.s8",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s8, addr_nop, ee_load_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vadds.s16",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s16, addr_nop, ee_load_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vadds.s32",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s32, addr_nop, ee_load_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VADDS.s[8/16/32].LD.INCP qz,qx,qy
    {
        .name = "ee.vadds.s8.ld.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s8, addr_inc16, ee_load_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vadds.s16.ld.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s16, addr_inc16, ee_load_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vadds.s32.ld.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s32, addr_inc16, ee_load_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VADDS.s[8/16/32].ST.INCP qz,qx,qy
    {
        .name = "ee.vadds.s8.st.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s8, addr_inc16, ee_store_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vadds.s16.st.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s16, addr_inc16, ee_store_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vadds.s32.st.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s32, addr_inc16, ee_store_op, ee_add_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VSUBS ...
    // VSUBS.s[8/16/32] qz,qx,qy
    {
        .name = "ee.vsubs.s8",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s8, addr_nop, ee_load_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsubs.s16",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s16, addr_nop, ee_load_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsubs.s32",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s32, addr_nop, ee_load_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VSUBS.s[8/16/32].LD.INCP qz,qx,qy
    {
        .name = "ee.vsubs.s8.ld.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s8, addr_inc16, ee_load_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsubs.s16.ld.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s16, addr_inc16, ee_load_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsubs.s32.ld.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s32, addr_inc16, ee_load_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VSUBS.s[8/16/32].ST.INCP qz,qx,qy
    {
        .name = "ee.vsubs.s8.st.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s8, addr_inc16, ee_store_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsubs.s16.st.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s16, addr_inc16, ee_store_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsubs.s32.st.incp",
        .translate = translate_vadds_s3,
        .par = (const uint32_t[]){ldqa_s32, addr_inc16, ee_store_op, ee_sub_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VMUL
    {
        .name = "ee.vmul.s8",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_s8, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.s16",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_s16, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.u8",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_u8, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.u16",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_u16, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Load: ee.vmul.xx.ld.incp
    {
        .name = "ee.vmul.s8.ld.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_s8, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.s16.ld.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_s16, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.u8.ld.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_u8, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.u16.ld.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_u16, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Store: ee.vmul.xx.st.incp
    {
        .name = "ee.vmul.s8.st.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_s8, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.s16.st.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_s16, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.u8.st.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_u8, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmul.u16.st.incp",
        .translate = translate_vmul_s3,
        .par = (const uint32_t[]){vmul_u16, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // CMUL
    // ee.cmul.s16  qz, qx, qy, sel4
    {
        .name = "ee.cmul.s16",
        .translate = translate_cmul_s3,
        .par = (const uint32_t[]){addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.cmul.s16.ld.incp",
        .translate = translate_cmul_s3,
        .par = (const uint32_t[]){addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.cmul.s16.st.incp",
        .translate = translate_cmul_s3,
        .par = (const uint32_t[]){addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Multiply and accumulate
    // EE.VMULAS.[U/S][8/16].ACCX       qx, qy
    {
        .name = "ee.vmulas.s8.accx",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s8, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.accx",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u8, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.accx",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s16, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.accx",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u16, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].ACCX.LD.IP       qu, as, imm16, qx, qy
    {
        .name = "ee.vmulas.s8.accx.ld.ip",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s8, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.accx.ld.ip",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u8, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.accx.ld.ip",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s16, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.accx.ld.ip",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u16, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].ACCX.LD.xP       qu, as, ad, qx, qy
    {
        .name = "ee.vmulas.s8.accx.ld.xp",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s8, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.accx.ld.xp",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u8, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.accx.ld.xp",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s16, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.accx.ld.xp",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u16, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].ACCX.LD.IP.qup       qu, as, imm16, qx, qy, qs0, qs1
    {
        .name = "ee.vmulas.s8.accx.ld.ip.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s8, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.accx.ld.ip.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u8, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.accx.ld.ip.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s16, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.accx.ld.ip.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u16, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].ACCX.LD.xP.qup       qu, as, ad, qx, qy , qs0, qs1
    {
        .name = "ee.vmulas.s8.accx.ld.xp.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s8, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.accx.ld.xp.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u8, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.accx.ld.xp.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_s16, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.accx.ld.xp.qup",
        .translate = translate_vmulas_accx_s3,
        .par = (const uint32_t[]){vmul_u16, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].QACC       qx, qy
    {
        .name = "ee.vmulas.s8.qacc",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.qacc",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.qacc",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u8, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.qacc",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u16, addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].QACC.LD.IP       qu, as, imm16, qx, qy
    {
        .name = "ee.vmulas.s8.qacc.ld.ip",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.qacc.ld.ip",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u8, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.qacc.ld.ip",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.qacc.ld.ip",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u16, addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].QACC.LD.xP       qu, as, ad, qx, qy
    {
        .name = "ee.vmulas.s8.qacc.ld.xp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.qacc.ld.xp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u8, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.qacc.ld.xp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.qacc.ld.xp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u16, addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].QACC.ldbc.incp       qu, as, qx, qy
    {
        .name = "ee.vmulas.s8.qacc.ldbc.incp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_ldbc_inc1, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.qacc.ldbc.incp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u8, addr_ldbc_inc1, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.qacc.ldbc.incp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_ldbc_inc1, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.qacc.ldbc.incp",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u16, addr_ldbc_inc1, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].QACC.ldbc.incp.qup       qu, as, qx, qy, qs0, qs1
    {
        .name = "ee.vmulas.s8.qacc.ldbc.incp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_ldbc_inc1, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.qacc.ldbc.incp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u8, addr_ldbc_inc1, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.qacc.ldbc.incp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_ldbc_inc1, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.qacc.ldbc.incp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u16, addr_ldbc_inc1, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].QACC.LD.IP.QUP       qu, as, imm16, qx, qy, qs0, qs1
    {
        .name = "ee.vmulas.s8.qacc.ld.ip.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.qacc.ld.ip.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u8, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.qacc.ld.ip.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.qacc.ld.ip.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u16, addr_ip, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMULAS.[U/S][8/16].QACC.LD.XP.QUP       qu, as, ad, qx, qy, qs0, qs1
    {
        .name = "ee.vmulas.s8.qacc.ld.xp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u8.qacc.ld.xp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u8, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.s16.qacc.ld.xp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmulas.u16.qacc.ld.xp.qup",
        .translate = translate_vmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_u16, addr_xp, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // ee.vsmulas.s[8/16].qacc
    {
        .name = "ee.vsmulas.s8.qacc",
        .translate = translate_vsmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsmulas.s16.qacc",
        .translate = translate_vsmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // ee.vsmulas.s[8/16].qacc.ld.incp
    {
        .name = "ee.vsmulas.s8.qacc.ld.incp",
        .translate = translate_vsmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s8, addr_inc16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vsmulas.s16.qacc.ld.incp",
        .translate = translate_vsmulas_qacc_s3,
        .par = (const uint32_t[]){vmul_s16, addr_inc16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Others..
    // EE.SRCMB.S[8/16].QACC        qu, as, sel2
    {
        .name = "ee.srcmb.s8.qacc",
        .translate = translate_srcmb_qacc_s3,
        .par = (const uint32_t[]){vmul_s8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.srcmb.s16.qacc",
        .translate = translate_srcmb_qacc_s3,
        .par = (const uint32_t[]){vmul_s16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VRELU.S[8/16]       qs, ax, ay
    {
        .name = "ee.vrelu.s8",
        .translate = translate_vrelu_s3,
        .par = (const uint32_t[]){vmul_s8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vrelu.s16",
        .translate = translate_vrelu_s3,
        .par = (const uint32_t[]){vmul_s16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VPRELU.S[8/16]       qz, qx, qy, ay
    {
        .name = "ee.vprelu.s8",
        .translate = translate_vprelu_s3,
        .par = (const uint32_t[]){vmul_s8},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vprelu.s16",
        .translate = translate_vprelu_s3,
        .par = (const uint32_t[]){vmul_s16},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Comparison...
    // EE.VMAX.[S8/S16/S32]     qa, qx, qy
    {
        .name = "ee.vmax.s8",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s8, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmax.s16",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s16, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmax.s32",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s32, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMAX.[S8/S16/S32].LD.INCP      qu, as, qa, qx, qy
    {
        .name = "ee.vmax.s8.ld.incp",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s8, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmax.s16.ld.incp",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s16, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmax.s32.ld.incp",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s32, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMAX.[S8/S16/S32].ST.INCP       qu, as, qa, qx, qy
    {
        .name = "ee.vmax.s8.st.incp",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s8, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmax.s16.st.incp",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s16, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmax.s32.st.incp",
        .translate = translate_vmax_s3,
        .par = (const uint32_t[]){vmul_s32, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Min operations
    // EE.VMIN.[S8/S16/S32]       qa, qx, qy
    {
        .name = "ee.vmin.s8",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s8, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmin.s16",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s16, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmin.s32",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s32, addr_nop, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMIN.[S8/S16/S32].LD.INCP     qu, as, qa, qx, qy
    {
        .name = "ee.vmin.s8.ld.incp",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s8, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmin.s16.ld.incp",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s16, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmin.s32.ld.incp",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s32, addr_inc16, ee_load_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VMIN.[S8/S16/S32].ST.INCP      qv, as, qa, qx, qy
    {
        .name = "ee.vmin.s8.st.incp",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s8, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmin.s16.st.incp",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s16, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vmin.s32.st.incp",
        .translate = translate_vmin_s3,
        .par = (const uint32_t[]){vmul_s32, addr_inc16, ee_store_op},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // VCMP...
    // EE.VCMP.EQ.[S8/S16/S32]     qa, qx, qy
    {
        .name = "ee.vcmp.eq.s8",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s8, ee_vcmp_eq},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vcmp.eq.s16",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s16, ee_vcmp_eq},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vcmp.eq.s32",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s32, ee_vcmp_eq},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VCMP.LT.[S8/S16/S32]     qa, qx, qy
    {
        .name = "ee.vcmp.lt.s8",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s8, ee_vcmp_lt},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vcmp.lt.s16",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s16, ee_vcmp_lt},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vcmp.lt.s32",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s32, ee_vcmp_lt},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VCMP.GT.[S8/S16/S32]     qa, qx, qy
    {
        .name = "ee.vcmp.gt.s8",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s8, ee_vcmp_gt},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vcmp.gt.s16",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s16, ee_vcmp_gt},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.vcmp.gt.s32",
        .translate = translate_vcmp_s3,
        .par = (const uint32_t[]){vmul_s32, ee_vcmp_gt},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Bitwise logical
    // EE.ORQ      qa, qx, qy
    {
        .name = "ee.orq",
        .translate = translate_bw_logic_s3,
        .par = (const uint32_t[]){bw_logic_or},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.XORQ       qa, qx, qy
    {
        .name = "ee.xorq",
        .translate = translate_bw_logic_s3,
        .par = (const uint32_t[]){bw_logic_xor},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.ANDQ       qa, qx, qy
    {
        .name = "ee.andq",
        .translate = translate_bw_logic_s3,
        .par = (const uint32_t[]){bw_logic_and},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.NOTQ	qa, qx
    {
        .name = "ee.notq",
        .translate = translate_bw_logic_s3,
        .par = (const uint32_t[]){bw_logic_not},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Shift
    // EE.SRC.Q	qa, qs0, qs1
    {
        .name = "ee.src.q",
        .translate = translate_src_q_s3,
        .par = (const uint32_t[]){addr_nop, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SRC.Q.QUP	qa, qs0, qs1
    {
        .name = "ee.src.q.qup",
        .translate = translate_src_q_s3,
        .par = (const uint32_t[]){addr_nop, vmul_qup},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SRC.Q.LD.XP	qu, as, ad, qs0, qs1
    {
        .name = "ee.src.q.ld.xp",
        .translate = translate_src_q_s3,
        .par = (const uint32_t[]){addr_xp, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SRC.Q.LD.IP	qu, as, imm16, qs0, qs1
    {
        .name = "ee.src.q.ld.ip",
        .translate = translate_src_q_s3,
        .par = (const uint32_t[]){addr_ip, vmul_nop},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SLCI.2Q	qs1, qs0, sar16
    {
        .name = "ee.slci.2q",
        .translate = translate_sxci_2q_s3,
        .par = (const uint32_t[]){bw_shift_left},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SRCI.2Q	qs1, qs0, sar16
    {
        .name = "ee.srci.2q",
        .translate = translate_sxci_2q_s3,
        .par = (const uint32_t[]){bw_shift_right},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SLCXXP.2Q		qs1, qs0, as, ad
    {
        .name = "ee.slcxxp.2q",
        .translate = translate_sxcxxp_2q_s3,
        .par = (const uint32_t[]){bw_shift_left},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SRCXXP.2Q		qs1, qs0, as, ad
    {
        .name = "ee.srcxxp.2q",
        .translate = translate_sxcxxp_2q_s3,
        .par = (const uint32_t[]){bw_shift_right},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SRCQ.128.ST.INCP	qs0, qs1, as
    {
        .name = "ee.srcq.128.st.incp",
        .translate = translate_srcq_128_st_s3,
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VSR.32	qa, qs
    {
        .name = "ee.vsr.32",
        .translate = translate_vsx32_s3,
        .par = (const uint32_t[]){bw_shift_right},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.VSL.32	qa, qs
    {
        .name = "ee.vsl.32",
        .translate = translate_vsx32_s3,
        .par = (const uint32_t[]){bw_shift_left},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // FFT
    // Butterfly operation
    // EE.FFT.R2BF.S16.ST.INCP	qa0, qx, qy, as, sar4
    {
        .name = "ee.fft.r2bf.s16.st.incp",
        .translate = translate_r2bf_st_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.FFT.R2BF.S16	qa0, qa1, qx, qy, sel2
    {
        .name = "ee.fft.r2bf.s16",
        .translate = translate_r2bf_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.FFT.CMUL.S16.LD.XP	qu, as, ad, qz, qx, qy, sel8
    {
        .name = "ee.fft.cmul.s16.ld.xp",
        .translate = translate_fft_cmul_ld_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.FFT.CMUL.S16.ST.XP	qx, qy, qv, as, ad, sel8, upd4, sar4
    {
        .name = "ee.fft.cmul.s16.st.xp",
        .translate = translate_fft_cmul_st_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // Bit reverse
    // EE.BITREV	qa, as
    {
        .name = "ee.bitrev",
        .translate = translate_bitrev_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // FFTR
    // EE.FFT.AMS.S16.LD.INCP  qu, as, qz, qz1, qx, qy, qm, sel2
    {
        .name = "ee.fft.ams.s16.ld.incp",
        .translate = translate_fft_ams_s16_ld_incp,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.FFT.AMS.S16.ST.INCP	qv, qz1, as0, as, qx, qy, qm, sel2
    {
        .name = "ee.fft.ams.s16.st.incp",
        .translate = translate_fft_ams_s16_st_incp,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.FFT.AMS.S16.LD.INCP.UAUP	qu, as, qz, qz1, qx, qy, qm, sel2
    {
        .name = "ee.fft.ams.s16.ld.incp.uaup",
        .translate = translate_fft_ams_s16_ld_incp_uaup,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.FFT.AMS.S16.LD.R32.DECP qu, as, qz, qz1, qx, qy, qm, sel2
    {
        .name = "ee.fft.ams.s16.ld.r32.decp",
        .translate = translate_fft_ams_s16_ld_decp,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.FFT.VST.R32.DECP		qv, as, sar2
    {
        .name = "ee.fft.vst.r32.decp",
        .translate = translate_fft_vst_decp_s3,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },

    // GPIO
    // EE.WR_MASK_GPIO_OUT
    {
        .name = "ee.wr_mask_gpio_out",
        .translate = translate_wr_mask_gpio_out_s3,
        .par = (const uint32_t[]){gpio_mask},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.SET_BIT_GPIO_OUT
    {
        .name = "ee.set_bit_gpio_out",
        .translate = translate_wr_mask_gpio_out_s3,
        .par = (const uint32_t[]){gpio_set},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.CLR_BIT_GPIO_OUT
    {
        .name = "ee.clr_bit_gpio_out",
        .translate = translate_wr_mask_gpio_out_s3,
        .par = (const uint32_t[]){gpio_clr},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // EE.GET_GPIO_IN
    {
        .name = "ee.get_gpio_in",
        .translate = translate_wr_mask_gpio_out_s3,
        .par = (const uint32_t[]){gpio_in},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // ld.qr
    {
        .name = "ld.qr",
        .translate = translate_ld_qr_s3,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // st.qr
    {
        .name = "st.qr",
        .translate = translate_st_qr_s3,
        .par = (const uint32_t[]){addr_ip},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // mv.qr
    {
        .name = "mv.qr",
        .translate = translate_mv_qr_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // ee.ld.accx.ip
    {
        .name = "ee.ld.accx.ip",
        .translate = translate_ld_accx_ip_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // ee.ld.accx.ip
    {
        .name = "ee.srs.accx",
        .translate = translate_srs_accx_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // ld.qacc_x.h.32.ip
    {
        .name = "ee.ld.qacc_h.h.32.ip",
        .translate = translate_ld_qacc_x_h_32_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_h_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ld.qacc_l.h.32.ip",
        .translate = translate_ld_qacc_x_h_32_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_l_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ld.qacc_h.l.128.ip",
        .translate = translate_ld_qacc_x_l_128_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_h_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.ld.qacc_l.l.128.ip",
        .translate = translate_ld_qacc_x_l_128_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_l_0},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    // st.qacc_x.h.32.ip / st.qacc_x.l.128.ip
    {
        .name = "ee.st.qacc_h.h.32.ip",
        .translate = translate_st_qacc_x_h_32_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_h_0},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.st.qacc_l.h.32.ip",
        .translate = translate_st_qacc_x_h_32_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_l_0},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.st.qacc_h.l.128.ip",
        .translate = translate_st_qacc_x_l_128_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_h_0},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.st.qacc_l.l.128.ip",
        .translate = translate_st_qacc_x_l_128_ip_s3,
        .par = (const uint32_t[]){wrur_qacc_l_0},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    // ee.st.accx.ip
    {
        .name = "ee.st.accx.ip",
        .translate = translate_st_accx_ip_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    // ee.ld.ua_state.ip / ee.st.ua_state.ip
    {
        .name = "ee.ld.ua_state.ip",
        .translate = translate_ld_ua_state_ip_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.st.ua_state.ip",
        .translate = translate_st_ua_state_ip_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
    // ee.ldxq.32 / ee.stxq.32
    {
        .name = "ee.ldxq.32",
        .translate = translate_ldxq_32_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x0,
    },
    {
        .name = "ee.stxq.32",
        .translate = translate_stxq_32_s3,
        .par = (const uint32_t[]){},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x0,
    },
};

const XtensaOpcodeTranslators xtensa_tie_opcodes = {
    .num_opcodes = ARRAY_SIZE(tie_ops),
    .opcode = tie_ops,
};

DEF_HELPER_2(exception, noreturn, env, i32)
DEF_HELPER_3(exception_cause, noreturn, env, i32, i32)
DEF_HELPER_4(exception_cause_vaddr, noreturn, env, i32, i32, i32)
DEF_HELPER_3(debug_exception, noreturn, env, i32, i32)

DEF_HELPER_1(sync_windowbase, void, env)
DEF_HELPER_4(entry, void, env, i32, i32, i32)
DEF_HELPER_2(test_ill_retw, void, env, i32)
DEF_HELPER_2(test_underflow_retw, void, env, i32)
DEF_HELPER_2(retw, void, env, i32)
DEF_HELPER_3(window_check, noreturn, env, i32, i32)
DEF_HELPER_1(restore_owb, void, env)
DEF_HELPER_2(movsp, void, env, i32)
#ifndef CONFIG_USER_ONLY
DEF_HELPER_1(simcall, void, env)
#endif

#ifndef CONFIG_USER_ONLY
DEF_HELPER_3(waiti, void, env, i32, i32)
DEF_HELPER_1(update_ccount, void, env)
DEF_HELPER_2(wsr_ccount, void, env, i32)
DEF_HELPER_2(update_ccompare, void, env, i32)
DEF_HELPER_1(check_interrupts, void, env)
DEF_HELPER_2(intset, void, env, i32)
DEF_HELPER_2(intclear, void, env, i32)
DEF_HELPER_3(check_atomctl, void, env, i32, i32)
DEF_HELPER_4(check_exclusive, void, env, i32, i32, i32)
DEF_HELPER_2(wsr_memctl, void, env, i32)

DEF_HELPER_2(itlb_hit_test, void, env, i32)
DEF_HELPER_2(wsr_rasid, void, env, i32)
DEF_HELPER_FLAGS_3(rtlb0, TCG_CALL_NO_RWG_SE, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(rtlb1, TCG_CALL_NO_RWG_SE, i32, env, i32, i32)
DEF_HELPER_3(itlb, void, env, i32, i32)
DEF_HELPER_3(ptlb, i32, env, i32, i32)
DEF_HELPER_4(wtlb, void, env, i32, i32, i32)
DEF_HELPER_2(wsr_mpuenb, void, env, i32)
DEF_HELPER_3(wptlb, void, env, i32, i32)
DEF_HELPER_FLAGS_2(rptlb0, TCG_CALL_NO_RWG_SE, i32, env, i32)
DEF_HELPER_FLAGS_2(rptlb1, TCG_CALL_NO_RWG_SE, i32, env, i32)
DEF_HELPER_2(pptlb, i32, env, i32)

DEF_HELPER_2(wsr_ibreakenable, void, env, i32)
DEF_HELPER_3(wsr_ibreaka, void, env, i32, i32)
DEF_HELPER_3(wsr_dbreaka, void, env, i32, i32)
DEF_HELPER_3(wsr_dbreakc, void, env, i32, i32)
#endif

DEF_HELPER_2(wur_fpu2k_fcr, void, env, i32)
DEF_HELPER_FLAGS_1(abs_s, TCG_CALL_NO_RWG_SE, f32, f32)
DEF_HELPER_FLAGS_1(neg_s, TCG_CALL_NO_RWG_SE, f32, f32)
DEF_HELPER_3(fpu2k_add_s, f32, env, f32, f32)
DEF_HELPER_3(fpu2k_sub_s, f32, env, f32, f32)
DEF_HELPER_3(fpu2k_mul_s, f32, env, f32, f32)
DEF_HELPER_4(fpu2k_madd_s, f32, env, f32, f32, f32)
DEF_HELPER_4(fpu2k_msub_s, f32, env, f32, f32, f32)
DEF_HELPER_4(ftoi_s, i32, env, f32, i32, i32)
DEF_HELPER_4(ftoui_s, i32, env, f32, i32, i32)
DEF_HELPER_3(itof_s, f32, env, i32, i32)
DEF_HELPER_3(uitof_s, f32, env, i32, i32)
DEF_HELPER_2(cvtd_s, f64, env, f32)

DEF_HELPER_3(un_s,  i32, env, f32, f32)
DEF_HELPER_3(oeq_s, i32, env, f32, f32)
DEF_HELPER_3(ueq_s, i32, env, f32, f32)
DEF_HELPER_3(olt_s, i32, env, f32, f32)
DEF_HELPER_3(ult_s, i32, env, f32, f32)
DEF_HELPER_3(ole_s, i32, env, f32, f32)
DEF_HELPER_3(ule_s, i32, env, f32, f32)

DEF_HELPER_2(wur_fpu_fcr, void, env, i32)
DEF_HELPER_1(rur_fpu_fsr, i32, env)
DEF_HELPER_2(wur_fpu_fsr, void, env, i32)
DEF_HELPER_FLAGS_1(abs_d, TCG_CALL_NO_RWG_SE, f64, f64)
DEF_HELPER_FLAGS_1(neg_d, TCG_CALL_NO_RWG_SE, f64, f64)
DEF_HELPER_3(add_d, f64, env, f64, f64)
DEF_HELPER_3(add_s, f32, env, f32, f32)
DEF_HELPER_3(sub_d, f64, env, f64, f64)
DEF_HELPER_3(sub_s, f32, env, f32, f32)
DEF_HELPER_3(mul_d, f64, env, f64, f64)
DEF_HELPER_3(mul_s, f32, env, f32, f32)
DEF_HELPER_4(madd_d, f64, env, f64, f64, f64)
DEF_HELPER_4(madd_s, f32, env, f32, f32, f32)
DEF_HELPER_4(msub_d, f64, env, f64, f64, f64)
DEF_HELPER_4(msub_s, f32, env, f32, f32, f32)
DEF_HELPER_3(mkdadj_d, f64, env, f64, f64)
DEF_HELPER_3(mkdadj_s, f32, env, f32, f32)
DEF_HELPER_2(mksadj_d, f64, env, f64)
DEF_HELPER_2(mksadj_s, f32, env, f32)
DEF_HELPER_4(ftoi_d, i32, env, f64, i32, i32)
DEF_HELPER_4(ftoui_d, i32, env, f64, i32, i32)
DEF_HELPER_3(itof_d, f64, env, i32, i32)
DEF_HELPER_3(uitof_d, f64, env, i32, i32)
DEF_HELPER_2(cvts_d, f32, env, f64)

DEF_HELPER_3(un_d,  i32, env, f64, f64)
DEF_HELPER_3(oeq_d, i32, env, f64, f64)
DEF_HELPER_3(ueq_d, i32, env, f64, f64)
DEF_HELPER_3(olt_d, i32, env, f64, f64)
DEF_HELPER_3(ult_d, i32, env, f64, f64)
DEF_HELPER_3(ole_d, i32, env, f64, f64)
DEF_HELPER_3(ule_d, i32, env, f64, f64)

DEF_HELPER_2(rer, i32, env, i32)
DEF_HELPER_3(wer, void, env, i32, i32)

DEF_HELPER_4(vld_64_s3, void, env, i32, i64, i32)
DEF_HELPER_3(vst_64_s3, i64, env, i32, i32)
DEF_HELPER_4(fft_vst_64_s3, i64, env, i32, i32, i32)
DEF_HELPER_4(vldbc_s3, void, env, i32, i32, i32)
DEF_HELPER_3(vldhbc_16_s3, void, env, i32, i64)
DEF_HELPER_2(set_sar_byte_s3, void, env, i32)
DEF_HELPER_4(ldqa_64_s3, void, env, i32, i64, i32)
DEF_HELPER_1(dump_all_s3, void, env)
DEF_HELPER_3(zero_s3, void, env, i32, i32)
DEF_HELPER_3(wur_s3, void, env, i32, i32)
DEF_HELPER_2(rur_s3, i32, env, i32)
DEF_HELPER_3(mov_qacc_s3, void, env, i32, i32)
DEF_HELPER_3(movi_a_s3, i32, env, i32, i32)
DEF_HELPER_4(movi_q_s3, void, env, i32, i32, i32)
DEF_HELPER_4(vzip_s3, void, env, i32, i32, i32)
DEF_HELPER_4(vunzip_s3, void, env, i32, i32, i32)

DEF_HELPER_5(vadds_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_5(vsubs_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_6(vmul_s3, void, env, i32, i32, i32, i32, i32)
DEF_HELPER_6(cmul_s3, void, env, i32, i32, i32, i32, i32)

DEF_HELPER_4(vmulas_accx_s3, void, env, i32, i32, i32)
DEF_HELPER_4(vmulas_qacc_s3, void, env, i32, i32, i32)
DEF_HELPER_3(vmulas_qup_s3, void, env, i32, i32)
DEF_HELPER_5(vsmulas_s3, void, env, i32, i32, i32, i32)

DEF_HELPER_5(srcmb_qacc_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_5(src_q_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_5(vrelu_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_6(vprelu_s3, void, env, i32, i32, i32, i32, i32)

DEF_HELPER_5(vmax_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_5(vmin_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_6(vcmp_s3, void, env, i32, i32, i32, i32, i32)

DEF_HELPER_5(bw_logic_s3, void, env, i32, i32, i32, i32)

DEF_HELPER_5(sxci_2q_s3, void, env, i32, i32, i32, i32)
DEF_HELPER_4(srcq_64_rd_s3, i64, env, i32, i32, i32)
DEF_HELPER_5(vsx32_s3, void, env, i32, i32, i32, i32)

DEF_HELPER_5(r2bf_st_low_s3, i64, env, i32, i32, i32, i32)
DEF_HELPER_5(r2bf_st_high_s3, i64, env, i32, i32, i32, i32)

DEF_HELPER_6(r2bf_s3, void, env, i32, i32, i32, i32, i32)

DEF_HELPER_6(fft_cmul_ld_s3, void, env, i32, i32, i32, i32, i32)

DEF_HELPER_8(fft_cmul_st_low_s3, i64, env, i32, i32, i32, i32, i32, i32, i32)
DEF_HELPER_8(fft_cmul_st_high_s3, i64, env, i32, i32, i32, i32, i32, i32, i32)

DEF_HELPER_3(bitrev_s3, void, env, i32, i32)

DEF_HELPER_8(fft_ams_s16, void, env, i32, i32, i32, i32, i32, i32, i32)

DEF_HELPER_8(fft_ams_st_0_s16_low, i64, env, i32, i32, i32, i32, i32, i32, i32)
DEF_HELPER_8(fft_ams_st_1_s16_low, i64, env, i32, i32, i32, i32, i32, i32, i32)
DEF_HELPER_8(fft_ams_st_0_s16_high, i64, env, i32, i32, i32, i32, i32, i32, i32)
DEF_HELPER_8(fft_ams_st_1_s16_high, i64, env, i32, i32, i32, i32, i32, i32, i32)


DEF_HELPER_8(fft_ams_s16_uqup, void, env, i32, i32, i32, i32, i32, i32, i32)
DEF_HELPER_4(fft_ams_s16_ld_incp_uaup, void, env, i32, i64, i64)
DEF_HELPER_8(fft_ams_s16_decp, void, env, i32, i32, i32, i32, i32, i32, i32)
DEF_HELPER_2(fft_ams_s16_exchange_q, void, env, i32)

DEF_HELPER_1(fft_ams_st_s16_at, i32, env)

DEF_HELPER_4(wr_mask_gpio_out_s3, i32, env, i32, i32, i32)

DEF_HELPER_3(mv_qr_s3, void, env, i32, i32)

DEF_HELPER_2(ld_accx_s3, void, env, i64)
DEF_HELPER_2(srs_accx_s3, i32, env, i32)

DEF_HELPER_3(ld_qacc_x_h_32_ip_s3, void, env, i32, i32)

DEF_HELPER_4(ld_qacc_x_l_128_ip_s3, void, env, i32, i32, i64)

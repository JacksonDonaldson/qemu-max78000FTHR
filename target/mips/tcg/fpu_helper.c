/*
 *  Helpers for emulation of FPU-related MIPS instructions.
 *
 *  Copyright (C) 2004-2005  Jocelyn Mayer
 *  Copyright (C) 2020  Wave Computing, Inc.
 *  Copyright (C) 2020  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "fpu_helper.h"


/* Complex FPU operations which may need stack space. */

#define FLOAT_TWO32 make_float32(1 << 30)
#define FLOAT_TWO64 make_float64(1ULL << 62)

#define FP_TO_INT32_OVERFLOW 0x7fffffff
#define FP_TO_INT64_OVERFLOW 0x7fffffffffffffffULL

target_ulong helper_cfc1(CPUMIPSState *env, uint32_t reg)
{
    target_ulong arg1 = 0;

    switch (reg) {
    case 0:
        arg1 = (int32_t)env->active_fpu.fcr0;
        break;
    case 1:
        /* UFR Support - Read Status FR */
        if (env->active_fpu.fcr0 & (1 << FCR0_UFRP)) {
            if (env->CP0_Config5 & (1 << CP0C5_UFR)) {
                arg1 = (int32_t)
                       ((env->CP0_Status & (1  << CP0St_FR)) >> CP0St_FR);
            } else {
                do_raise_exception(env, EXCP_RI, GETPC());
            }
        }
        break;
    case 5:
        /* FRE Support - read Config5.FRE bit */
        if (env->active_fpu.fcr0 & (1 << FCR0_FREP)) {
            if (env->CP0_Config5 & (1 << CP0C5_UFE)) {
                arg1 = (env->CP0_Config5 >> CP0C5_FRE) & 1;
            } else {
                helper_raise_exception(env, EXCP_RI);
            }
        }
        break;
    case 25:
        arg1 = ((env->active_fpu.fcr31 >> 24) & 0xfe) |
               ((env->active_fpu.fcr31 >> 23) & 0x1);
        break;
    case 26:
        arg1 = env->active_fpu.fcr31 & 0x0003f07c;
        break;
    case 28:
        arg1 = (env->active_fpu.fcr31 & 0x00000f83) |
               ((env->active_fpu.fcr31 >> 22) & 0x4);
        break;
    default:
        arg1 = (int32_t)env->active_fpu.fcr31;
        break;
    }

    return arg1;
}

void helper_ctc1(CPUMIPSState *env, target_ulong arg1, uint32_t fs, uint32_t rt)
{
    switch (fs) {
    case 1:
        /* UFR Alias - Reset Status FR */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_UFRP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFR)) {
            env->CP0_Status &= ~(1 << CP0St_FR);
            compute_hflags(env);
        } else {
            do_raise_exception(env, EXCP_RI, GETPC());
        }
        break;
    case 4:
        /* UNFR Alias - Set Status FR */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_UFRP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFR)) {
            env->CP0_Status |= (1 << CP0St_FR);
            compute_hflags(env);
        } else {
            do_raise_exception(env, EXCP_RI, GETPC());
        }
        break;
    case 5:
        /* FRE Support - clear Config5.FRE bit */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_FREP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFE)) {
            env->CP0_Config5 &= ~(1 << CP0C5_FRE);
            compute_hflags(env);
        } else {
            helper_raise_exception(env, EXCP_RI);
        }
        break;
    case 6:
        /* FRE Support - set Config5.FRE bit */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_FREP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFE)) {
            env->CP0_Config5 |= (1 << CP0C5_FRE);
            compute_hflags(env);
        } else {
            helper_raise_exception(env, EXCP_RI);
        }
        break;
    case 25:
        if ((env->insn_flags & ISA_MIPS_R6) || (arg1 & 0xffffff00)) {
            return;
        }
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0x017fffff) |
                                ((arg1 & 0xfe) << 24) |
                                ((arg1 & 0x1) << 23);
        break;
    case 26:
        if (arg1 & 0x007c0000) {
            return;
        }
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfffc0f83) |
                                (arg1 & 0x0003f07c);
        break;
    case 28:
        if (arg1 & 0x007c0000) {
            return;
        }
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfefff07c) |
                                (arg1 & 0x00000f83) |
                                ((arg1 & 0x4) << 22);
        break;
    case 31:
        env->active_fpu.fcr31 = (arg1 & env->active_fpu.fcr31_rw_bitmask) |
               (env->active_fpu.fcr31 & ~(env->active_fpu.fcr31_rw_bitmask));
        break;
    default:
        if (env->insn_flags & ISA_MIPS_R6) {
            do_raise_exception(env, EXCP_RI, GETPC());
        }
        return;
    }
    restore_fp_status(env);
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    if ((GET_FP_ENABLE(env->active_fpu.fcr31) | 0x20) &
        GET_FP_CAUSE(env->active_fpu.fcr31)) {
        do_raise_exception(env, EXCP_FPE, GETPC());
    }
}

static inline int ieee_to_mips_xcpt(int ieee_xcpt)
{
    int mips_xcpt = 0;

    if (ieee_xcpt & float_flag_invalid) {
        mips_xcpt |= FP_INVALID;
    }
    if (ieee_xcpt & float_flag_overflow) {
        mips_xcpt |= FP_OVERFLOW;
    }
    if (ieee_xcpt & float_flag_underflow) {
        mips_xcpt |= FP_UNDERFLOW;
    }
    if (ieee_xcpt & float_flag_divbyzero) {
        mips_xcpt |= FP_DIV0;
    }
    if (ieee_xcpt & float_flag_inexact) {
        mips_xcpt |= FP_INEXACT;
    }

    return mips_xcpt;
}

static inline void update_fcr31(CPUMIPSState *env, uintptr_t pc)
{
    int ieee_exception_flags = get_float_exception_flags(
                                   &env->active_fpu.fp_status);
    int mips_exception_flags = 0;

    if (ieee_exception_flags) {
        mips_exception_flags = ieee_to_mips_xcpt(ieee_exception_flags);
    }

    SET_FP_CAUSE(env->active_fpu.fcr31, mips_exception_flags);

    if (mips_exception_flags)  {
        set_float_exception_flags(0, &env->active_fpu.fp_status);

        if (GET_FP_ENABLE(env->active_fpu.fcr31) & mips_exception_flags) {
            do_raise_exception(env, EXCP_FPE, pc);
        } else {
            UPDATE_FP_FLAGS(env->active_fpu.fcr31, mips_exception_flags);
        }
    }
}

/*
 * Float support.
 * Single precition routines have a "s" suffix, double precision a
 * "d" suffix, 32bit integer "w", 64bit integer "l", paired single "ps",
 * paired single lower "pl", paired single upper "pu".
 */

/* unary operations, modifying fp status  */
uint64_t helper_float_sqrt_d(CPUMIPSState *env, uint64_t fdt0)
{
    fdt0 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt0;
}

uint32_t helper_float_sqrt_s(CPUMIPSState *env, uint32_t fst0)
{
    fst0 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst0;
}

uint64_t helper_float_cvtd_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t fdt2;

    fdt2 = float32_to_float64(fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint64_t helper_float_cvtd_w(CPUMIPSState *env, uint32_t wt0)
{
    uint64_t fdt2;

    fdt2 = int32_to_float64(wt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint64_t helper_float_cvtd_l(CPUMIPSState *env, uint64_t dt0)
{
    uint64_t fdt2;

    fdt2 = int64_to_float64(dt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint64_t helper_float_cvt_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_cvt_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_cvtps_pw(CPUMIPSState *env, uint64_t dt0)
{
    uint32_t fst2;
    uint32_t fsth2;

    fst2 = int32_to_float32(dt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    fsth2 = int32_to_float32(dt0 >> 32, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_cvtpw_ps(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;
    uint32_t wth2;
    int excp, excph;

    wt2 = float32_to_int32(fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    excp = get_float_exception_flags(&env->active_fpu.fp_status);
    if (excp & (float_flag_overflow | float_flag_invalid)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    wth2 = float32_to_int32(fdt0 >> 32, &env->active_fpu.fp_status);
    excph = get_float_exception_flags(&env->active_fpu.fp_status);
    if (excph & (float_flag_overflow | float_flag_invalid)) {
        wth2 = FP_TO_INT32_OVERFLOW;
    }

    set_float_exception_flags(excp | excph, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());

    return ((uint64_t)wth2 << 32) | wt2;
}

uint32_t helper_float_cvts_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t fst2;

    fst2 = float64_to_float32(fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint32_t helper_float_cvts_w(CPUMIPSState *env, uint32_t wt0)
{
    uint32_t fst2;

    fst2 = int32_to_float32(wt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint32_t helper_float_cvts_l(CPUMIPSState *env, uint64_t dt0)
{
    uint32_t fst2;

    fst2 = int64_to_float32(dt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint32_t helper_float_cvts_pl(CPUMIPSState *env, uint32_t wt0)
{
    uint32_t wt2;

    wt2 = wt0;
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvts_pu(CPUMIPSState *env, uint32_t wth0)
{
    uint32_t wt2;

    wt2 = wth0;
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvt_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvt_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_round_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_round_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_round_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_round_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_trunc_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64_round_to_zero(fdt0,
                                         &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_trunc_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_trunc_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32_round_to_zero(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_trunc_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_ceil_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_ceil_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_ceil_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_ceil_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_floor_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_floor_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_floor_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_floor_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_cvt_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_cvt_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_cvt_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvt_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_round_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_round_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_round_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_round_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_trunc_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64_round_to_zero(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_trunc_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_trunc_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32_round_to_zero(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_trunc_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_ceil_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_ceil_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_ceil_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_ceil_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_floor_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_floor_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_floor_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_floor_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

/* unary operations, not modifying fp status  */

uint64_t helper_float_abs_d(uint64_t fdt0)
{
   return float64_abs(fdt0);
}

uint32_t helper_float_abs_s(uint32_t fst0)
{
    return float32_abs(fst0);
}

uint64_t helper_float_abs_ps(uint64_t fdt0)
{
    uint32_t wt0;
    uint32_t wth0;

    wt0 = float32_abs(fdt0 & 0XFFFFFFFF);
    wth0 = float32_abs(fdt0 >> 32);
    return ((uint64_t)wth0 << 32) | wt0;
}

uint64_t helper_float_chs_d(uint64_t fdt0)
{
   return float64_chs(fdt0);
}

uint32_t helper_float_chs_s(uint32_t fst0)
{
    return float32_chs(fst0);
}

uint64_t helper_float_chs_ps(uint64_t fdt0)
{
    uint32_t wt0;
    uint32_t wth0;

    wt0 = float32_chs(fdt0 & 0XFFFFFFFF);
    wth0 = float32_chs(fdt0 >> 32);
    return ((uint64_t)wth0 << 32) | wt0;
}

/* MIPS specific unary operations */
uint64_t helper_float_recip_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_div(float64_one, fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_recip_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_div(float32_one, fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_rsqrt_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    fdt2 = float64_div(float64_one, fdt2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_rsqrt_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    fst2 = float32_div(float32_one, fst2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_recip1_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_div(float64_one, fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_recip1_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_div(float32_one, fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_recip1_ps(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t fstl2;
    uint32_t fsth2;

    fstl2 = float32_div(float32_one, fdt0 & 0XFFFFFFFF,
                        &env->active_fpu.fp_status);
    fsth2 = float32_div(float32_one, fdt0 >> 32, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fstl2;
}

uint64_t helper_float_rsqrt1_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    fdt2 = float64_div(float64_one, fdt2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_rsqrt1_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    fst2 = float32_div(float32_one, fst2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_rsqrt1_ps(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t fstl2;
    uint32_t fsth2;

    fstl2 = float32_sqrt(fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    fsth2 = float32_sqrt(fdt0 >> 32, &env->active_fpu.fp_status);
    fstl2 = float32_div(float32_one, fstl2, &env->active_fpu.fp_status);
    fsth2 = float32_div(float32_one, fsth2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fstl2;
}

uint64_t helper_float_rint_d(CPUMIPSState *env, uint64_t fs)
{
    uint64_t fdret;

    fdret = float64_round_to_int(fs, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdret;
}

uint32_t helper_float_rint_s(CPUMIPSState *env, uint32_t fs)
{
    uint32_t fdret;

    fdret = float32_round_to_int(fs, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdret;
}

#define FLOAT_CLASS_SIGNALING_NAN      0x001
#define FLOAT_CLASS_QUIET_NAN          0x002
#define FLOAT_CLASS_NEGATIVE_INFINITY  0x004
#define FLOAT_CLASS_NEGATIVE_NORMAL    0x008
#define FLOAT_CLASS_NEGATIVE_SUBNORMAL 0x010
#define FLOAT_CLASS_NEGATIVE_ZERO      0x020
#define FLOAT_CLASS_POSITIVE_INFINITY  0x040
#define FLOAT_CLASS_POSITIVE_NORMAL    0x080
#define FLOAT_CLASS_POSITIVE_SUBNORMAL 0x100
#define FLOAT_CLASS_POSITIVE_ZERO      0x200

uint64_t float_class_d(uint64_t arg, float_status *status)
{
    if (float64_is_signaling_nan(arg, status)) {
        return FLOAT_CLASS_SIGNALING_NAN;
    } else if (float64_is_quiet_nan(arg, status)) {
        return FLOAT_CLASS_QUIET_NAN;
    } else if (float64_is_neg(arg)) {
        if (float64_is_infinity(arg)) {
            return FLOAT_CLASS_NEGATIVE_INFINITY;
        } else if (float64_is_zero(arg)) {
            return FLOAT_CLASS_NEGATIVE_ZERO;
        } else if (float64_is_zero_or_denormal(arg)) {
            return FLOAT_CLASS_NEGATIVE_SUBNORMAL;
        } else {
            return FLOAT_CLASS_NEGATIVE_NORMAL;
        }
    } else {
        if (float64_is_infinity(arg)) {
            return FLOAT_CLASS_POSITIVE_INFINITY;
        } else if (float64_is_zero(arg)) {
            return FLOAT_CLASS_POSITIVE_ZERO;
        } else if (float64_is_zero_or_denormal(arg)) {
            return FLOAT_CLASS_POSITIVE_SUBNORMAL;
        } else {
            return FLOAT_CLASS_POSITIVE_NORMAL;
        }
    }
}

uint64_t helper_float_class_d(CPUMIPSState *env, uint64_t arg)
{
    return float_class_d(arg, &env->active_fpu.fp_status);
}

uint32_t float_class_s(uint32_t arg, float_status *status)
{
    if (float32_is_signaling_nan(arg, status)) {
        return FLOAT_CLASS_SIGNALING_NAN;
    } else if (float32_is_quiet_nan(arg, status)) {
        return FLOAT_CLASS_QUIET_NAN;
    } else if (float32_is_neg(arg)) {
        if (float32_is_infinity(arg)) {
            return FLOAT_CLASS_NEGATIVE_INFINITY;
        } else if (float32_is_zero(arg)) {
            return FLOAT_CLASS_NEGATIVE_ZERO;
        } else if (float32_is_zero_or_denormal(arg)) {
            return FLOAT_CLASS_NEGATIVE_SUBNORMAL;
        } else {
            return FLOAT_CLASS_NEGATIVE_NORMAL;
        }
    } else {
        if (float32_is_infinity(arg)) {
            return FLOAT_CLASS_POSITIVE_INFINITY;
        } else if (float32_is_zero(arg)) {
            return FLOAT_CLASS_POSITIVE_ZERO;
        } else if (float32_is_zero_or_denormal(arg)) {
            return FLOAT_CLASS_POSITIVE_SUBNORMAL;
        } else {
            return FLOAT_CLASS_POSITIVE_NORMAL;
        }
    }
}

uint32_t helper_float_class_s(CPUMIPSState *env, uint32_t arg)
{
    return float_class_s(arg, &env->active_fpu.fp_status);
}

/* binary operations */

uint64_t helper_float_add_d(CPUMIPSState *env,
                            uint64_t fdt0, uint64_t fdt1)
{
    uint64_t dt2;

    dt2 = float64_add(fdt0, fdt1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_add_s(CPUMIPSState *env,
                            uint32_t fst0, uint32_t fst1)
{
    uint32_t wt2;

    wt2 = float32_add(fst0, fst1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_add_ps(CPUMIPSState *env,
                             uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t wtl2;
    uint32_t wth2;

    wtl2 = float32_add(fstl0, fstl1, &env->active_fpu.fp_status);
    wth2 = float32_add(fsth0, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)wth2 << 32) | wtl2;
}

uint64_t helper_float_sub_d(CPUMIPSState *env,
                            uint64_t fdt0, uint64_t fdt1)
{
    uint64_t dt2;

    dt2 = float64_sub(fdt0, fdt1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_sub_s(CPUMIPSState *env,
                            uint32_t fst0, uint32_t fst1)
{
    uint32_t wt2;

    wt2 = float32_sub(fst0, fst1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_sub_ps(CPUMIPSState *env,
                             uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t wtl2;
    uint32_t wth2;

    wtl2 = float32_sub(fstl0, fstl1, &env->active_fpu.fp_status);
    wth2 = float32_sub(fsth0, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)wth2 << 32) | wtl2;
}

uint64_t helper_float_mul_d(CPUMIPSState *env,
                            uint64_t fdt0, uint64_t fdt1)
{
    uint64_t dt2;

    dt2 = float64_mul(fdt0, fdt1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_mul_s(CPUMIPSState *env,
                            uint32_t fst0, uint32_t fst1)
{
    uint32_t wt2;

    wt2 = float32_mul(fst0, fst1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_mul_ps(CPUMIPSState *env,
                             uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t wtl2;
    uint32_t wth2;

    wtl2 = float32_mul(fstl0, fstl1, &env->active_fpu.fp_status);
    wth2 = float32_mul(fsth0, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)wth2 << 32) | wtl2;
}

uint64_t helper_float_div_d(CPUMIPSState *env,
                            uint64_t fdt0, uint64_t fdt1)
{
    uint64_t dt2;

    dt2 = float64_div(fdt0, fdt1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_div_s(CPUMIPSState *env,
                            uint32_t fst0, uint32_t fst1)
{
    uint32_t wt2;

    wt2 = float32_div(fst0, fst1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_div_ps(CPUMIPSState *env,
                             uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t wtl2;
    uint32_t wth2;

    wtl2 = float32_div(fstl0, fstl1, &env->active_fpu.fp_status);
    wth2 = float32_div(fsth0, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)wth2 << 32) | wtl2;
}


/* MIPS specific binary operations */
uint64_t helper_float_recip2_d(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    fdt2 = float64_mul(fdt0, fdt2, &env->active_fpu.fp_status);
    fdt2 = float64_chs(float64_sub(fdt2, float64_one,
                                   &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_recip2_s(CPUMIPSState *env, uint32_t fst0, uint32_t fst2)
{
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_sub(fst2, float32_one,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_recip2_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fstl2 = float32_mul(fstl0, fstl2, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fsth0, fsth2, &env->active_fpu.fp_status);
    fstl2 = float32_chs(float32_sub(fstl2, float32_one,
                                       &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_sub(fsth2, float32_one,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fstl2;
}

uint64_t helper_float_rsqrt2_d(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    fdt2 = float64_mul(fdt0, fdt2, &env->active_fpu.fp_status);
    fdt2 = float64_sub(fdt2, float64_one, &env->active_fpu.fp_status);
    fdt2 = float64_chs(float64_div(fdt2, FLOAT_TWO64,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_rsqrt2_s(CPUMIPSState *env, uint32_t fst0, uint32_t fst2)
{
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_sub(fst2, float32_one, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_div(fst2, FLOAT_TWO32,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_rsqrt2_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fstl2 = float32_mul(fstl0, fstl2, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fsth0, fsth2, &env->active_fpu.fp_status);
    fstl2 = float32_sub(fstl2, float32_one, &env->active_fpu.fp_status);
    fsth2 = float32_sub(fsth2, float32_one, &env->active_fpu.fp_status);
    fstl2 = float32_chs(float32_div(fstl2, FLOAT_TWO32,
                                       &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_div(fsth2, FLOAT_TWO32,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fstl2;
}

uint64_t helper_float_addr_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fstl2;
    uint32_t fsth2;

    fstl2 = float32_add(fstl0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_add(fstl1, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fstl2;
}

uint64_t helper_float_mulr_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fstl2;
    uint32_t fsth2;

    fstl2 = float32_mul(fstl0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fstl1, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fstl2;
}


uint32_t helper_float_max_s(CPUMIPSState *env, uint32_t fs, uint32_t ft)
{
    uint32_t fdret;

    fdret = float32_maxnum(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint64_t helper_float_max_d(CPUMIPSState *env, uint64_t fs, uint64_t ft)
{
    uint64_t fdret;

    fdret = float64_maxnum(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint32_t helper_float_maxa_s(CPUMIPSState *env, uint32_t fs, uint32_t ft)
{
    uint32_t fdret;

    fdret = float32_maxnummag(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint64_t helper_float_maxa_d(CPUMIPSState *env, uint64_t fs, uint64_t ft)
{
    uint64_t fdret;

    fdret = float64_maxnummag(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint32_t helper_float_min_s(CPUMIPSState *env, uint32_t fs, uint32_t ft)
{
    uint32_t fdret;

    fdret = float32_minnum(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint64_t helper_float_min_d(CPUMIPSState *env, uint64_t fs, uint64_t ft)
{
    uint64_t fdret;

    fdret = float64_minnum(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint32_t helper_float_mina_s(CPUMIPSState *env, uint32_t fs, uint32_t ft)
{
    uint32_t fdret;

    fdret = float32_minnummag(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint64_t helper_float_mina_d(CPUMIPSState *env, uint64_t fs, uint64_t ft)
{
    uint64_t fdret;

    fdret = float64_minnummag(fs, ft, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}


/* ternary operations */

uint64_t helper_float_madd_d(CPUMIPSState *env, uint64_t fst0,
                             uint64_t fst1, uint64_t fst2)
{
    fst0 = float64_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float64_add(fst0, fst2, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fst0;
}

uint32_t helper_float_madd_s(CPUMIPSState *env, uint32_t fst0,
                             uint32_t fst1, uint32_t fst2)
{
    fst0 = float32_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float32_add(fst0, fst2, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fst0;
}

uint64_t helper_float_madd_ps(CPUMIPSState *env, uint64_t fdt0,
                              uint64_t fdt1, uint64_t fdt2)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fstl2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fstl0 = float32_mul(fstl0, fstl1, &env->active_fpu.fp_status);
    fstl0 = float32_add(fstl0, fstl2, &env->active_fpu.fp_status);
    fsth0 = float32_mul(fsth0, fsth1, &env->active_fpu.fp_status);
    fsth0 = float32_add(fsth0, fsth2, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return ((uint64_t)fsth0 << 32) | fstl0;
}

uint64_t helper_float_msub_d(CPUMIPSState *env, uint64_t fst0,
                             uint64_t fst1, uint64_t fst2)
{
    fst0 = float64_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float64_sub(fst0, fst2, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fst0;
}

uint32_t helper_float_msub_s(CPUMIPSState *env, uint32_t fst0,
                             uint32_t fst1, uint32_t fst2)
{
    fst0 = float32_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float32_sub(fst0, fst2, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fst0;
}

uint64_t helper_float_msub_ps(CPUMIPSState *env, uint64_t fdt0,
                              uint64_t fdt1, uint64_t fdt2)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fstl2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fstl0 = float32_mul(fstl0, fstl1, &env->active_fpu.fp_status);
    fstl0 = float32_sub(fstl0, fstl2, &env->active_fpu.fp_status);
    fsth0 = float32_mul(fsth0, fsth1, &env->active_fpu.fp_status);
    fsth0 = float32_sub(fsth0, fsth2, &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return ((uint64_t)fsth0 << 32) | fstl0;
}

uint64_t helper_float_nmadd_d(CPUMIPSState *env, uint64_t fst0,
                             uint64_t fst1, uint64_t fst2)
{
    fst0 = float64_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float64_add(fst0, fst2, &env->active_fpu.fp_status);
    fst0 = float64_chs(fst0);

    update_fcr31(env, GETPC());
    return fst0;
}

uint32_t helper_float_nmadd_s(CPUMIPSState *env, uint32_t fst0,
                             uint32_t fst1, uint32_t fst2)
{
    fst0 = float32_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float32_add(fst0, fst2, &env->active_fpu.fp_status);
    fst0 = float32_chs(fst0);

    update_fcr31(env, GETPC());
    return fst0;
}

uint64_t helper_float_nmadd_ps(CPUMIPSState *env, uint64_t fdt0,
                              uint64_t fdt1, uint64_t fdt2)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fstl2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fstl0 = float32_mul(fstl0, fstl1, &env->active_fpu.fp_status);
    fstl0 = float32_add(fstl0, fstl2, &env->active_fpu.fp_status);
    fstl0 = float32_chs(fstl0);
    fsth0 = float32_mul(fsth0, fsth1, &env->active_fpu.fp_status);
    fsth0 = float32_add(fsth0, fsth2, &env->active_fpu.fp_status);
    fsth0 = float32_chs(fsth0);

    update_fcr31(env, GETPC());
    return ((uint64_t)fsth0 << 32) | fstl0;
}

uint64_t helper_float_nmsub_d(CPUMIPSState *env, uint64_t fst0,
                             uint64_t fst1, uint64_t fst2)
{
    fst0 = float64_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float64_sub(fst0, fst2, &env->active_fpu.fp_status);
    fst0 = float64_chs(fst0);

    update_fcr31(env, GETPC());
    return fst0;
}

uint32_t helper_float_nmsub_s(CPUMIPSState *env, uint32_t fst0,
                             uint32_t fst1, uint32_t fst2)
{
    fst0 = float32_mul(fst0, fst1, &env->active_fpu.fp_status);
    fst0 = float32_sub(fst0, fst2, &env->active_fpu.fp_status);
    fst0 = float32_chs(fst0);

    update_fcr31(env, GETPC());
    return fst0;
}

uint64_t helper_float_nmsub_ps(CPUMIPSState *env, uint64_t fdt0,
                              uint64_t fdt1, uint64_t fdt2)
{
    uint32_t fstl0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fstl1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fstl2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fstl0 = float32_mul(fstl0, fstl1, &env->active_fpu.fp_status);
    fstl0 = float32_sub(fstl0, fstl2, &env->active_fpu.fp_status);
    fstl0 = float32_chs(fstl0);
    fsth0 = float32_mul(fsth0, fsth1, &env->active_fpu.fp_status);
    fsth0 = float32_sub(fsth0, fsth2, &env->active_fpu.fp_status);
    fsth0 = float32_chs(fsth0);

    update_fcr31(env, GETPC());
    return ((uint64_t)fsth0 << 32) | fstl0;
}


uint32_t helper_float_maddf_s(CPUMIPSState *env, uint32_t fs,
                              uint32_t ft, uint32_t fd)
{
    uint32_t fdret;

    fdret = float32_muladd(fs, ft, fd, 0,
                           &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint64_t helper_float_maddf_d(CPUMIPSState *env, uint64_t fs,
                              uint64_t ft, uint64_t fd)
{
    uint64_t fdret;

    fdret = float64_muladd(fs, ft, fd, 0,
                           &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint32_t helper_float_msubf_s(CPUMIPSState *env, uint32_t fs,
                              uint32_t ft, uint32_t fd)
{
    uint32_t fdret;

    fdret = float32_muladd(fs, ft, fd, float_muladd_negate_product,
                           &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}

uint64_t helper_float_msubf_d(CPUMIPSState *env, uint64_t fs,
                              uint64_t ft, uint64_t fd)
{
    uint64_t fdret;

    fdret = float64_muladd(fs, ft, fd, float_muladd_negate_product,
                           &env->active_fpu.fp_status);

    update_fcr31(env, GETPC());
    return fdret;
}


/* compare operations */
#define FOP_COND_D(op, cond)                                   \
void helper_cmp_d_ ## op(CPUMIPSState *env, uint64_t fdt0,     \
                         uint64_t fdt1, int cc)                \
{                                                              \
    int c;                                                     \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}                                                              \
void helper_cmpabs_d_ ## op(CPUMIPSState *env, uint64_t fdt0,  \
                            uint64_t fdt1, int cc)             \
{                                                              \
    int c;                                                     \
    fdt0 = float64_abs(fdt0);                                  \
    fdt1 = float64_abs(fdt1);                                  \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered_quiet() is still called.
 */
FOP_COND_D(f,    (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_D(un,   float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status))
FOP_COND_D(eq,   float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ueq,  float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(olt,  float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ult,  float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ole,  float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ule,  float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered() is still called.
 */
FOP_COND_D(sf,   (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_D(ngle, float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status))
FOP_COND_D(seq,  float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ngl,  float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(lt,   float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(nge,  float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(le,   float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ngt,  float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status))

#define FOP_COND_S(op, cond)                                   \
void helper_cmp_s_ ## op(CPUMIPSState *env, uint32_t fst0,     \
                         uint32_t fst1, int cc)                \
{                                                              \
    int c;                                                     \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}                                                              \
void helper_cmpabs_s_ ## op(CPUMIPSState *env, uint32_t fst0,  \
                            uint32_t fst1, int cc)             \
{                                                              \
    int c;                                                     \
    fst0 = float32_abs(fst0);                                  \
    fst1 = float32_abs(fst1);                                  \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called.
 */
FOP_COND_S(f,    (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_S(un,   float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status))
FOP_COND_S(eq,   float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ueq,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(olt,  float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ult,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ole,  float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ule,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called.
 */
FOP_COND_S(sf,   (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_S(ngle, float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status))
FOP_COND_S(seq,  float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ngl,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(lt,   float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(nge,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(le,   float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ngt,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status))

#define FOP_COND_PS(op, condl, condh)                           \
void helper_cmp_ps_ ## op(CPUMIPSState *env, uint64_t fdt0,     \
                          uint64_t fdt1, int cc)                \
{                                                               \
    uint32_t fst0, fsth0, fst1, fsth1;                          \
    int ch, cl;                                                 \
    fst0 = fdt0 & 0XFFFFFFFF;                                   \
    fsth0 = fdt0 >> 32;                                         \
    fst1 = fdt1 & 0XFFFFFFFF;                                   \
    fsth1 = fdt1 >> 32;                                         \
    cl = condl;                                                 \
    ch = condh;                                                 \
    update_fcr31(env, GETPC());                                 \
    if (cl)                                                     \
        SET_FP_COND(cc, env->active_fpu);                       \
    else                                                        \
        CLEAR_FP_COND(cc, env->active_fpu);                     \
    if (ch)                                                     \
        SET_FP_COND(cc + 1, env->active_fpu);                   \
    else                                                        \
        CLEAR_FP_COND(cc + 1, env->active_fpu);                 \
}                                                               \
void helper_cmpabs_ps_ ## op(CPUMIPSState *env, uint64_t fdt0,  \
                             uint64_t fdt1, int cc)             \
{                                                               \
    uint32_t fst0, fsth0, fst1, fsth1;                          \
    int ch, cl;                                                 \
    fst0 = float32_abs(fdt0 & 0XFFFFFFFF);                      \
    fsth0 = float32_abs(fdt0 >> 32);                            \
    fst1 = float32_abs(fdt1 & 0XFFFFFFFF);                      \
    fsth1 = float32_abs(fdt1 >> 32);                            \
    cl = condl;                                                 \
    ch = condh;                                                 \
    update_fcr31(env, GETPC());                                 \
    if (cl)                                                     \
        SET_FP_COND(cc, env->active_fpu);                       \
    else                                                        \
        CLEAR_FP_COND(cc, env->active_fpu);                     \
    if (ch)                                                     \
        SET_FP_COND(cc + 1, env->active_fpu);                   \
    else                                                        \
        CLEAR_FP_COND(cc + 1, env->active_fpu);                 \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called.
 */
FOP_COND_PS(f,    (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status), 0),
                  (float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_PS(un,   float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(eq,   float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_eq_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ueq,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_eq_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(olt,  float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_lt_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ult,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_lt_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ole,  float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_le_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ule,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_le_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called.
 */
FOP_COND_PS(sf,   (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status), 0),
                  (float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_PS(ngle, float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(seq,  float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_eq(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ngl,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_eq(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(lt,   float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_lt(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(nge,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_lt(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(le,   float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_le(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ngt,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_le(fsth0, fsth1,
                                       &env->active_fpu.fp_status))

/* R6 compare operations */
#define FOP_CONDN_D(op, cond)                                       \
uint64_t helper_r6_cmp_d_ ## op(CPUMIPSState *env, uint64_t fdt0,   \
                                uint64_t fdt1)                      \
{                                                                   \
    uint64_t c;                                                     \
    c = cond;                                                       \
    update_fcr31(env, GETPC());                                     \
    if (c) {                                                        \
        return -1;                                                  \
    } else {                                                        \
        return 0;                                                   \
    }                                                               \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered_quiet() is still called.
 */
FOP_CONDN_D(af,  (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_D(un,  (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(eq,  (float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ueq, (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(lt,  (float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ult, (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(le,  (float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ule, (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered() is still called.\
 */
FOP_CONDN_D(saf,  (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_D(sun,  (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(seq,  (float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sueq, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(slt,  (float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sult, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sle,  (float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sule, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(or,   (float64_le_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(une,  (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ne,   (float64_lt_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sor,  (float64_le(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sune, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sne,  (float64_lt(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))

#define FOP_CONDN_S(op, cond)                                       \
uint32_t helper_r6_cmp_s_ ## op(CPUMIPSState *env, uint32_t fst0,   \
                                uint32_t fst1)                      \
{                                                                   \
    uint64_t c;                                                     \
    c = cond;                                                       \
    update_fcr31(env, GETPC());                                     \
    if (c) {                                                        \
        return -1;                                                  \
    } else {                                                        \
        return 0;                                                   \
    }                                                               \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called.
 */
FOP_CONDN_S(af,   (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_S(un,   (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(eq,   (float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ueq,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(lt,   (float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ult,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(le,   (float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ule,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called.
 */
FOP_CONDN_S(saf,  (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_S(sun,  (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(seq,  (float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sueq, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(slt,  (float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sult, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sle,  (float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sule, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(or,   (float32_le_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(une,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ne,   (float32_lt_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sor,  (float32_le(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sune, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sne,  (float32_lt(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))

/**
 * Copyright (C) Tactical Computing Labs, LLC. 2022. ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if defined(__riscv)

#include <ucs/arch/cpu.h>
#include <ucm/bistro/bistro.h>
#include <ucm/bistro/bistro_int.h>
#include <ucs/debug/assert.h>
#include <ucs/sys/math.h>
#include <ucm/util/sys.h>

#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Registers numbers to use with the move immediate to register.
 * The destination register is X31 (highest temporary).
 * Register X28-X30 are used for block shifting and masking.
 * Register X0 is always zero
 */
#define X31 31
#define X30 30
#define X0  0

/**
  * @brief JALR - Add 12 bit immediate to source register, save to destination
  * register, jump and link from destination register
  *
  * @param[in] _regs source register number (0-31)
  * @param[in] _regd destination register number (0-31)
  * @param[in] _imm 12 bit immmediate value
  */
#define JALR(_regs, _regd, _imm) \
    (((_imm) << 20) | ((_regs) << 15) | (0b000 << 12) | ((_regd) << 7) | (0x67))

/**
  * @brief C_J - Indirect jump (using compressed instruction)
  *
  * @param[in] _imm 12 bit immmediate value
  */
#define C_J(_imm) \
    ((0b101) << 13 | ((_imm >> 1) << 2) | (0b01))

/**
  * @brief ADDI - Add 12 bit immediate to source register, save to destination
  * register
  *
  * @param[in] _regs source register number (0-31)
  * @param[in] _regd destination register number (0-31)
  * @param[in] _imm 12 bit immmediate value
  */
#define ADDI(_regs, _regd, _imm) \
    (((_imm) << 20) | ((_regs) << 15) | (0b000 << 12) | ((_regd) << 7) | (0x13))

/**
  * @brief ADD - Add two registers together
  *
  * @param[in] _regs_a first source register number (0-31)
  * @param[in] _regs_b second source register number (0-31)
  * @param[in] _regd destination register number (0-31)
  */
#define ADD(_regs_a, _regs_b, _regd) \
    ((_regs_b << 20) | (_regs_a << 15) | (0b000 << 12) | ((_regd) << 7) | \
     (0x33))

/**
  * @brief LUI - load upper 20 bit immediate to destination register
  *
  * @param[in] _regd register number (0-31)
  * @param[in] _imm 12 bit immmediate value
  */
#define LUI(_regd, _imm) (((_imm) << 12) | ((_regd) << 7) | (0x37))

/**
  * @brief SLLI - left-shift immediate number of bits in source register into
  * destination register
  *
  * @param[in] _regs source register number (0-31)
  * @param[in] _regd destination register number (0-31)
  * @param[in] _imm 12 bit immmediate value
  */
#define SLLI(_regs, _regd, _imm) \
    (((_imm) << 20) | ((_regs) << 15) | (0b001 << 12) | ((_regd) << 7) | (0x13))

void ucm_bistro_patch_lock(void *dst)
{
    static const ucm_bistro_lock_t self_jmp = {
        .j = C_J(0)
    };
    ucm_bistro_modify_code(dst, &self_jmp);
}

ucs_status_t ucm_bistro_patch(void *func_ptr, void *hook, const char *symbol,
                              void **orig_func_p,
                              ucm_bistro_restore_point_t **rp)
{
    ucs_status_t status;
    uintptr_t hookp = (uintptr_t)hook;
    ucm_bistro_patch_t patch;
    uint32_t hookp_upper, hookp_lower;

    /*
     * Account for the fact that JALR, ADD, and ADDI sign extend and may result
     * in subtractions by adding extra to compensate.
     */
    hookp_upper = (uint32_t)(hookp >> 32) + ((uint32_t)(hookp >> 31) & 1);
    hookp_lower = (uint32_t) hookp;

    patch = (ucm_bistro_patch_t) {
      .rega = LUI(X31, ((hookp_upper >> 12) + ((hookp_upper >> 11) & 1)) & 0xFFFFF),
      .regb = ADDI(X31, X31, hookp_upper & 0xFFF),
      .regc = SLLI(X31, X31, 32),
      .regd = LUI(X30, ((hookp_lower >> 12) + ((hookp_lower >> 11) & 1)) & 0xFFFFF),
      .rege = ADD(X31, X31, X30),
      .regf = JALR(X31, X0, hookp_lower & 0xFFF),
    };

    if (orig_func_p != NULL) {
        return UCS_ERR_UNSUPPORTED;
    }

    status = ucm_bistro_create_restore_point(func_ptr, sizeof(patch), rp);
    if (UCS_STATUS_IS_ERR(status)) {
        return status;
    }

    return ucm_bistro_apply_patch_atomic(func_ptr, &patch, sizeof(patch));
}

ucs_status_t ucm_bistro_relocate_one(ucm_bistro_relocate_context_t *ctx)
{
    return UCS_ERR_UNSUPPORTED;
}

#endif

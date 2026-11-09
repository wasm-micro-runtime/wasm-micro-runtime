/*
 * Copyright (C) 2021 XiaoMi Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_reloc.h"

#define R_RISCV_32 1
#define R_RISCV_64 2
#define R_RISCV_CALL 18
#define R_RISCV_CALL_PLT 19
#define R_RISCV_PCREL_HI20 23
#define R_RISCV_PCREL_LO12_I 24
#define R_RISCV_PCREL_LO12_S 25
#define R_RISCV_HI20 26
#define R_RISCV_LO12_I 27
#define R_RISCV_LO12_S 28

#define RV_OPCODE_SW 0x23

#undef NEED_SOFT_FP
#undef NEED_SOFT_DP
#undef NEED_SOFT_I32_MUL
#undef NEED_SOFT_I32_DIV
#undef NEED_SOFT_I64_MUL
#undef NEED_SOFT_I64_DIV
#undef NEED_SOFT_ATOMIC

#ifdef __riscv_flen
#if __riscv_flen == 32
#define NEED_SOFT_DP
#endif
#else
#define NEED_SOFT_FP
#define NEED_SOFT_DP
#endif

#ifndef __riscv_mul
#define NEED_SOFT_I32_MUL
#define NEED_SOFT_I64_MUL
#elif __riscv_xlen == 32
#define NEED_SOFT_I64_MUL
#endif

#ifndef __riscv_div
#define NEED_SOFT_I32_DIV
#define NEED_SOFT_I64_DIV
#elif __riscv_xlen == 32
#define NEED_SOFT_I64_DIV
#endif

#ifndef __riscv_atomic
#define NEED_SOFT_ATOMIC
#endif

/* clang-format off */
void __adddf3(void);
void __addsf3(void);
void __divdf3(void);
void __divdi3(void);
void __divsf3(void);
void __divsi3(void);
void __eqdf2(void);
void __eqsf2(void);
void __extendsfdf2(void);
void __fixdfdi(void);
void __fixdfsi(void);
void __fixsfdi(void);
void __fixsfsi(void);
void __fixunsdfdi(void);
void __fixunsdfsi(void);
void __fixunssfdi(void);
void __fixunssfsi(void);
void __floatdidf(void);
void __floatdisf(void);
void __floatsidf(void);
void __floatsisf(void);
void __floatundidf(void);
void __floatundisf(void);
void __floatunsidf(void);
void __floatunsisf(void);
void __gedf2(void);
void __gesf2(void);
void __gtdf2(void);
void __gtsf2(void);
void __ledf2(void);
void __lesf2(void);
void __ltdf2(void);
void __ltsf2(void);
void __moddi3(void);
void __modsi3(void);
void __muldf3(void);
void __muldi3(void);
void __mulsf3(void);
void __mulsi3(void);
void __nedf2(void);
void __negdf2(void);
void __negsf2(void);
void __nesf2(void);
void __subdf3(void);
void __subsf3(void);
void __truncdfsf2(void);
void __udivdi3(void);
void __udivsi3(void);
void __umoddi3(void);
void __umodsi3(void);
void __unorddf2(void);
void __unordsf2(void);
bool __atomic_compare_exchange_4(volatile void *, void *, unsigned int,
                                 bool, int, int);
void __atomic_store_4(volatile void *, unsigned int, int);
/* clang-format on */

static SymbolMap target_sym_map[] = {
    /* clang-format off */
    REG_COMMON_SYMBOLS
#ifdef NEED_SOFT_FP
    REG_SYM(__addsf3),
    REG_SYM(__divsf3),
    REG_SYM(__eqsf2),
    REG_SYM(__fixsfdi),
    REG_SYM(__fixunssfdi),
    REG_SYM(__fixunssfsi),
    REG_SYM(__floatsidf),
    REG_SYM(__gesf2),
    REG_SYM(__gtsf2),
    REG_SYM(__lesf2),
    REG_SYM(__mulsf3),
    REG_SYM(__negsf2),
    REG_SYM(__nesf2),
    REG_SYM(__subsf3),
    REG_SYM(__unordsf2),
#elif __riscv_xlen == 32
    /* rv32f, support FP instruction but need soft routines
     * to convert float and long long
     */
    REG_SYM(__floatundisf),
    REG_SYM(__floatdisf),
#endif
#ifdef NEED_SOFT_DP
    REG_SYM(__adddf3),
    REG_SYM(__divdf3),
    REG_SYM(__eqdf2),
    REG_SYM(__extendsfdf2),
    REG_SYM(__fixdfdi),
    REG_SYM(__fixdfsi),
    REG_SYM(__fixunsdfdi),
    REG_SYM(__fixunsdfsi),
    REG_SYM(__floatdidf),
    REG_SYM(__floatsidf),
    REG_SYM(__floatundidf),
    REG_SYM(__floatunsidf),
    REG_SYM(__gedf2),
    REG_SYM(__gtdf2),
    REG_SYM(__ledf2),
    REG_SYM(__ltdf2),
    REG_SYM(__muldf3),
    REG_SYM(__nedf2),
    REG_SYM(__negdf2),
    REG_SYM(__subdf3),
    REG_SYM(__truncdfsf2),
    REG_SYM(__unorddf2),
#elif __riscv_xlen == 32
    /* rv32d, support DP instruction but need soft routines
     * to convert double and long long
     */
    REG_SYM(__fixdfdi),
    REG_SYM(__floatundidf),
#endif
#ifdef NEED_SOFT_I32_MUL
    REG_SYM(__mulsi3),
#endif
#ifdef NEED_SOFT_I32_DIV
    REG_SYM(__divsi3),
    REG_SYM(__modsi3),
    REG_SYM(__udivsi3),
    REG_SYM(__umodsi3),
#endif
#ifdef NEED_SOFT_I64_MUL
    REG_SYM(__muldi3),
#endif
#ifdef NEED_SOFT_I64_DIV
    REG_SYM(__divdi3),
    REG_SYM(__moddi3),
    REG_SYM(__udivdi3),
    REG_SYM(__umoddi3),
#endif
#ifdef NEED_SOFT_ATOMIC
    REG_SYM(__atomic_compare_exchange_4),
    REG_SYM(__atomic_store_4),
#endif
    /* clang-format on */
};

/*
 * Cache entries for matching R_RISCV_PCREL_HI20 with its corresponding
 * R_RISCV_PCREL_LO12_{I,S}. The relocation table is typically ordered by
 * increasing offset, so only a small number of "in-flight" HI20 entries are
 * expected at any moment; 8 is a conservative fixed bound.
 */
#define PCREL_CACHE_SIZE 8

typedef struct {
    uintptr_t hi20_addr;
    uintptr_t cached_offset;
} pcrel_cache_entry_t;

static pcrel_cache_entry_t pcrel_cache[PCREL_CACHE_SIZE];
static int pcrel_cache_count = 0;

void
aot_reloc_reset_cache(void)
{
    int i;
    for (i = 0; i < PCREL_CACHE_SIZE; i++) {
        pcrel_cache[i].hi20_addr = 0;
        pcrel_cache[i].cached_offset = 0;
    }
    pcrel_cache_count = 0;
}

static bool
add_hi20_to_cache(uintptr_t hi20_reloc_addr, uintptr_t hi20_offset)
{
    int i;

    for (i = 0; i < PCREL_CACHE_SIZE; i++) {
        if (pcrel_cache[i].hi20_addr == 0) {
            pcrel_cache[i].hi20_addr = hi20_reloc_addr;
            pcrel_cache[i].cached_offset = hi20_offset;
            pcrel_cache_count++;
            return true;
        }
    }

    return false;
}

static uintptr_t
find_hi20_in_cache(uintptr_t hi20_reloc_addr)
{
    int i;

    for (i = 0; i < PCREL_CACHE_SIZE; i++) {
        if (pcrel_cache[i].hi20_addr == hi20_reloc_addr) {
            pcrel_cache[i].hi20_addr = 0;
            pcrel_cache[i].cached_offset = 0;
            pcrel_cache_count--;
            return pcrel_cache[i].cached_offset;
        }
    }
    return 0;
}

static inline bool
valid_hi20_imm(long imm_hi)
{
#if __riscv_xlen == 64
    long hi = imm_hi & ((1 << 20) - 1);
    long sign = -((imm_hi >> 19) & 1);
    hi = ((hi << 12) | sign << 32) >> 12;
    return imm_hi == hi;
#else
    return true;
#endif
}

static void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    if (error_buf != NULL)
        snprintf(error_buf, error_buf_size, "%s", string);
}

void
get_current_target(char *target_buf, uint32 target_buf_size)
{
    snprintf(target_buf, target_buf_size, "riscv");
}

uint32
get_plt_item_size(void)
{
#if __riscv_xlen == 64
    /* auipc + ld + jalr + nop + addr */
    return 20;
#else
    return 0;
#endif
}

SymbolMap *
get_target_symbol_map(uint32 *sym_num)
{
    *sym_num = sizeof(target_sym_map) / sizeof(SymbolMap);
    return target_sym_map;
}

/* Get a val from given address */
static uint32
rv_get_val(uint16 *addr)
{
    uint32 ret;
    ret = *addr | (*(addr + 1)) << 16;
    return ret;
}

/* Set a val to given address */
static void
rv_set_val(uint16 *addr, uint32 val)
{
    *addr = (val & 0xffff);
    *(addr + 1) = (val >> 16);

#ifdef __riscv_zifencei
    __asm__ volatile("fence.i");
#else
    __asm__ volatile("fence");
#endif
}

/* Add a val to given address */
static void
rv_add_val(uint16 *addr, uint32 val)
{
    uint32 cur = rv_get_val(addr);
    rv_set_val(addr, cur + val);
}

/**
 * Get imm_hi and imm_lo from given integer
 *
 * @param imm given integer (intptr_t, 32-bit on RV32, 64-bit on RV64)
 * @param imm_hi signed 20bit
 * @param imm_lo signed 12bit
 *
 */
static void
rv_calc_imm(intptr_t imm, int32 *imm_hi, int32 *imm_lo)
{
    intptr_t lo;
    intptr_t hi = imm / 4096;
    intptr_t r = imm % 4096;

    if (2047 < r) {
        hi++;
    }
    else if (r < -2048) {
        hi--;
    }

    lo = imm - (hi * 4096);

    *imm_lo = (int32)lo;
    *imm_hi = (int32)hi;
}

uint32
get_plt_table_size()
{
    return get_plt_item_size() * (sizeof(target_sym_map) / sizeof(SymbolMap));
}

void
init_plt_table(uint8 *plt)
{
#if __riscv_xlen == 64
    uint32 i, num = sizeof(target_sym_map) / sizeof(SymbolMap);
    uint8 *p;

    for (i = 0; i < num; i++) {
        p = plt;
        /* auipc t1, 0 */
        *(uint16 *)p = 0x0317;
        p += 2;
        *(uint16 *)p = 0x0000;
        p += 2;
        /* ld t1, 8(t1) */
        *(uint16 *)p = 0x3303;
        p += 2;
        *(uint16 *)p = 0x00C3;
        p += 2;
        /* jr t1 */
        *(uint16 *)p = 0x8302;
        p += 2;
        /* nop */
        *(uint16 *)p = 0x0001;
        p += 2;
        bh_memcpy_s(p, 8, &target_sym_map[i].symbol_addr, 8);
        p += 8;
        plt += get_plt_item_size();
    }
#endif
}

typedef struct RelocTypeStrMap {
    uint32 reloc_type;
    char *reloc_str;
} RelocTypeStrMap;

#define RELOC_TYPE_MAP(reloc_type) { reloc_type, #reloc_type }

static RelocTypeStrMap reloc_type_str_maps[] = {
    RELOC_TYPE_MAP(R_RISCV_32),           RELOC_TYPE_MAP(R_RISCV_64),
    RELOC_TYPE_MAP(R_RISCV_CALL),         RELOC_TYPE_MAP(R_RISCV_CALL_PLT),
    RELOC_TYPE_MAP(R_RISCV_PCREL_HI20),   RELOC_TYPE_MAP(R_RISCV_PCREL_LO12_I),
    RELOC_TYPE_MAP(R_RISCV_PCREL_LO12_S), RELOC_TYPE_MAP(R_RISCV_HI20),
    RELOC_TYPE_MAP(R_RISCV_LO12_I),       RELOC_TYPE_MAP(R_RISCV_LO12_S),
};

static const char *
reloc_type_to_str(uint32 reloc_type)
{
    uint32 i;

    for (i = 0; i < sizeof(reloc_type_str_maps) / sizeof(RelocTypeStrMap);
         i++) {
        if (reloc_type_str_maps[i].reloc_type == reloc_type)
            return reloc_type_str_maps[i].reloc_str;
    }

    return "Unknown_Reloc_Type";
}

static bool
check_reloc_offset(uint32 target_section_size, uint64 reloc_offset,
                   uint32 reloc_data_size, char *error_buf,
                   uint32 error_buf_size)
{
    if (!(reloc_offset < (uint64)target_section_size
          && reloc_offset + reloc_data_size <= (uint64)target_section_size)) {
        set_error_buf(error_buf, error_buf_size,
                      "AOT module load failed: invalid relocation offset.");
        return false;
    }
    return true;
}

bool
apply_relocation(AOTModule *module, uint8 *target_section_addr,
                 uint32 target_section_size, uint64 reloc_offset,
                 int64 reloc_addend, uint32 reloc_type, void *symbol_addr,
                 int32 symbol_index, char *error_buf, uint32 error_buf_size)
{
    int32 val, imm_hi, imm_lo, insn;
    uint8 *addr = target_section_addr + reloc_offset;
    char buf[128];

    switch (reloc_type) {
        case R_RISCV_32:
        {
            uint32 val_32 =
                (uint32)((uintptr_t)symbol_addr + (intptr_t)reloc_addend);

            CHECK_RELOC_OFFSET(sizeof(uint32));
            if (val_32 != ((uintptr_t)symbol_addr + (intptr_t)reloc_addend)) {
                goto fail_addr_out_of_range;
            }

            rv_set_val((uint16 *)addr, val_32);
            break;
        }

#if __riscv_xlen == 64
        case R_RISCV_64:
        {
            uint64 val_64 =
                (uint64)((intptr_t)symbol_addr + (intptr_t)reloc_addend);

            CHECK_RELOC_OFFSET(sizeof(uint64));
            if (val_64
                != (uint64)((intptr_t)symbol_addr + (intptr_t)reloc_addend)) {
                goto fail_addr_out_of_range;
            }

            bh_memcpy_s(addr, 8, &val_64, 8);
#ifdef __riscv_zifencei
            __asm__ volatile("fence.i");
#else
            __asm__ volatile("fence");
#endif
            break;
        }
#endif

        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
        case R_RISCV_PCREL_HI20: /* S + A - P */
        {
            val = (int32)(intptr_t)((uint8 *)symbol_addr + reloc_addend - addr);

            CHECK_RELOC_OFFSET(sizeof(uint32));
            if (val != (intptr_t)((uint8 *)symbol_addr + reloc_addend - addr)) {
                if (symbol_index >= 0) {
                    /* Call runtime function by plt code */
                    symbol_addr = (uint8 *)module->code + module->code_size
                                  - get_plt_table_size()
                                  + get_plt_item_size() * symbol_index;
                    val = (int32)(intptr_t)((uint8 *)symbol_addr - addr);
                }
            }

            if (val != (intptr_t)((uint8 *)symbol_addr + reloc_addend - addr)) {
                goto fail_addr_out_of_range;
            }

            rv_calc_imm(val, &imm_hi, &imm_lo);

            if (reloc_type == R_RISCV_PCREL_HI20) {
                if (!valid_hi20_imm(imm_hi)) {
                    set_error_buf(error_buf, error_buf_size,
                                  "AOT module load failed: invalid HI20 "
                                  "immediate for RV64");
                    return false;
                }
                if (!add_hi20_to_cache((uintptr_t)addr, (uintptr_t)val)) {
                    set_error_buf(error_buf, error_buf_size,
                                  "AOT module load failed: PCREL relocation "
                                  "cache overflow.");
                    return false;
                }
            }

            rv_add_val((uint16 *)addr, (imm_hi << 12));
            if ((rv_get_val((uint16 *)(addr + 4)) & 0x7f) == RV_OPCODE_SW) {
                /* Adjust imm for SW : S-type */
                val = (((int32)imm_lo >> 5) << 25)
                      + (((int32)imm_lo & 0x1f) << 7);

                rv_add_val((uint16 *)(addr + 4), val);
            }
            else {
                /* Adjust imm for MV(ADDI)/JALR : I-type */
                rv_add_val((uint16 *)(addr + 4), ((int32)imm_lo << 20));
            }
            break;
        }

        case R_RISCV_HI20: /* S + A */
        {
            CHECK_RELOC_OFFSET(sizeof(uint32));

            intptr_t val = (intptr_t)symbol_addr + (intptr_t)reloc_addend;
            int32_t imm_hi, imm_lo;
            rv_calc_imm(val, &imm_hi, &imm_lo);

            if (!valid_hi20_imm(imm_hi)) {
                set_error_buf(
                    error_buf, error_buf_size,
                    "AOT module load failed: invalid HI20 immediate for RV64");
                return false;
            }

            insn = rv_get_val((uint16 *)addr);
            insn = (insn & 0x00000fff) | (imm_hi << 12);
            rv_set_val((uint16 *)addr, insn);
            break;
        }

        case R_RISCV_PCREL_LO12_I: /* S - P */
        case R_RISCV_PCREL_LO12_S: /* S - P */
        {
            uintptr_t cached_offset;

            cached_offset = find_hi20_in_cache((uintptr_t)addr - 4);

            if (cached_offset != 0) {
                val = (int32)cached_offset;
            }
            else {
                val = (int32)(intptr_t)((uint8 *)symbol_addr + reloc_addend
                                        - addr - 4);
            }

            CHECK_RELOC_OFFSET(sizeof(uint32));

            rv_calc_imm(val, &imm_hi, &imm_lo);

            if (reloc_type == R_RISCV_PCREL_LO12_I) {
                rv_add_val((uint16 *)addr, ((int32)imm_lo << 20));
            }
            else {
                val = (((int32)imm_lo >> 5) << 25)
                      + (((int32)imm_lo & 0x1f) << 7);
                rv_add_val((uint16 *)addr, val);
            }
            break;
        }

        case R_RISCV_LO12_I: /* S + A */
        {
            CHECK_RELOC_OFFSET(sizeof(uint32));

            addr = target_section_addr + reloc_offset;
            insn = rv_get_val((uint16 *)addr);

            intptr_t val = (intptr_t)symbol_addr + (intptr_t)reloc_addend;
            int32_t imm_hi, imm_lo;
            rv_calc_imm(val, &imm_hi, &imm_lo);

            insn = (insn & 0x000fffff) | (imm_lo << 20);
            rv_set_val((uint16 *)addr, insn);
            break;
        }

        case R_RISCV_LO12_S:
        {
            CHECK_RELOC_OFFSET(sizeof(uint32));

            addr = target_section_addr + reloc_offset;

            intptr_t val = (intptr_t)symbol_addr + (intptr_t)reloc_addend;
            int32_t imm_hi, imm_lo;
            rv_calc_imm(val, &imm_hi, &imm_lo);

            val = (((int32)imm_lo >> 5) << 25) + (((int32)imm_lo & 0x1f) << 7);
            rv_add_val((uint16 *)addr, val);
            break;
        }

        default:
            if (error_buf != NULL)
                snprintf(error_buf, error_buf_size,
                         "Load relocation section failed: "
                         "invalid relocation type %" PRIu32 ".",
                         reloc_type);
            return false;
    }

    return true;

fail_addr_out_of_range:
    snprintf(buf, sizeof(buf),
             "AOT module load failed: "
             "relocation truncated to fit %s failed.",
             reloc_type_to_str(reloc_type));
    set_error_buf(error_buf, error_buf_size, buf);
    return false;
}

/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_reloc.h"

/*
 * Hexagon ELF relocation types.
 * Reference: "Qualcomm Hexagon Application Binary Interface User Guide"
 * https://docs.qualcomm.com/doc/80-N2040-23/80-N2040-23_REV_K_Qualcomm_Hexagon_Application_Binary_Interface_User_Guide.pdf
 */
#define R_HEX_NONE 0
#define R_HEX_B22_PCREL 1
#define R_HEX_B15_PCREL 2
#define R_HEX_B7_PCREL 3
#define R_HEX_LO16 4
#define R_HEX_HI16 5
#define R_HEX_32 6
#define R_HEX_16 7
#define R_HEX_8 8
#define R_HEX_GPREL16_0 9
#define R_HEX_GPREL16_1 10
#define R_HEX_GPREL16_2 11
#define R_HEX_GPREL16_3 12
#define R_HEX_B13_PCREL 14
#define R_HEX_B9_PCREL 15
#define R_HEX_B32_PCREL_X 16
#define R_HEX_32_6_X 17
#define R_HEX_B22_PCREL_X 18
#define R_HEX_B15_PCREL_X 19
#define R_HEX_B13_PCREL_X 20
#define R_HEX_B9_PCREL_X 21
#define R_HEX_B7_PCREL_X 22
#define R_HEX_16_X 23
#define R_HEX_12_X 24
#define R_HEX_11_X 25
#define R_HEX_10_X 26
#define R_HEX_9_X 27
#define R_HEX_8_X 28
#define R_HEX_7_X 29
#define R_HEX_6_X 30
#define R_HEX_32_PCREL 31
#define R_HEX_6_PCREL_X 65

/* Instruction bit-field masks for relocations. */
#define MASK_B22 0x01ff3ffe
#define MASK_B15 0x00df20fe
#define MASK_B13 0x00202ffe
#define MASK_B9 0x003000fe
#define MASK_B7 0x00001f18
#define MASK_LO16 0x00c03fff
#define MASK_X26 0x0fff3fff

/* Compiler runtime helpers required by AOT modules. */
/* clang-format off */
void __hexagon_divsi3(void);
void __hexagon_modsi3(void);
void __hexagon_udivsi3(void);
void __hexagon_umodsi3(void);
void __hexagon_divdi3(void);
void __hexagon_moddi3(void);
void __hexagon_udivdi3(void);
void __hexagon_umoddi3(void);
void __hexagon_udivmodsi4(void);
void __hexagon_udivmoddi4(void);

void __hexagon_divsf3(void);
void __hexagon_divdf3(void);
void __hexagon_adddf3(void);
void __hexagon_subdf3(void);
void __hexagon_muldf3(void);
void __hexagon_sqrtf(void);
void __hexagon_sqrtdf2(void);
void __hexagon_fmadf4(void);
void __hexagon_fmadf5(void);

void __hexagon_memcpy_likely_aligned_min32bytes_mult8bytes(void);
/* clang-format on */

/* clang-format off */
static SymbolMap target_sym_map[] = {
    REG_COMMON_SYMBOLS
    /* Integer division/modulo helpers */
    REG_SYM(__hexagon_divsi3),
    REG_SYM(__hexagon_modsi3),
    REG_SYM(__hexagon_udivsi3),
    REG_SYM(__hexagon_umodsi3),
    REG_SYM(__hexagon_divdi3),
    REG_SYM(__hexagon_moddi3),
    REG_SYM(__hexagon_udivdi3),
    REG_SYM(__hexagon_umoddi3),
    REG_SYM(__hexagon_udivmodsi4),
    REG_SYM(__hexagon_udivmoddi4),
    /* Floating-point helpers */
    REG_SYM(__hexagon_divsf3),
    REG_SYM(__hexagon_divdf3),
    REG_SYM(__hexagon_adddf3),
    REG_SYM(__hexagon_subdf3),
    REG_SYM(__hexagon_muldf3),
    REG_SYM(__hexagon_sqrtf),
    REG_SYM(__hexagon_sqrtdf2),
    REG_SYM(__hexagon_fmadf4),
    REG_SYM(__hexagon_fmadf5),
    /* Optimized memory operations */
    REG_SYM(__hexagon_memcpy_likely_aligned_min32bytes_mult8bytes),
};
/* clang-format on */

static void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    if (error_buf != NULL)
        snprintf(error_buf, error_buf_size, "%s", string);
}

SymbolMap *
get_target_symbol_map(uint32 *sym_num)
{
    *sym_num = sizeof(target_sym_map) / sizeof(SymbolMap);
    return target_sym_map;
}

void
get_current_target(char *target_buf, uint32 target_buf_size)
{
    snprintf(target_buf, target_buf_size, "hexagon");
}

#define INST_PARSE_PACKET_END 0x0000c000

static bool
is_duplex(uint32 insn)
{
    return (INST_PARSE_PACKET_END & insn) == 0;
}

/* Instruction opcode → relocation mask table for R_HEX_6_X */
/* clang-format off */
static const struct { uint32 cmp_mask; uint32 reloc_mask; } r6_masks[] = {
    { 0x38000000, 0x0000201f }, { 0x39000000, 0x0000201f },
    { 0x3e000000, 0x00001f80 }, { 0x3f000000, 0x00001f80 },
    { 0x40000000, 0x000020f8 }, { 0x41000000, 0x000007e0 },
    { 0x42000000, 0x000020f8 }, { 0x43000000, 0x000007e0 },
    { 0x44000000, 0x000020f8 }, { 0x45000000, 0x000007e0 },
    { 0x46000000, 0x000020f8 }, { 0x47000000, 0x000007e0 },
    { 0x6a000000, 0x00001f80 }, { 0x7c000000, 0x001f2000 },
    { 0x9a000000, 0x00000f60 }, { 0x9b000000, 0x00000f60 },
    { 0x9c000000, 0x00000f60 }, { 0x9d000000, 0x00000f60 },
    { 0x9f000000, 0x001f0100 }, { 0xab000000, 0x0000003f },
    { 0xad000000, 0x0000003f }, { 0xaf000000, 0x00030078 },
    { 0xd7000000, 0x006020e0 }, { 0xd8000000, 0x006020e0 },
    { 0xdb000000, 0x006020e0 }, { 0xdf000000, 0x006020e0 },
};
/* clang-format on */

static uint32
get_mask_r6(uint32 insn, char *error_buf, uint32 error_buf_size)
{
    uint32 i;

    if (is_duplex(insn))
        return 0x03f00000;

    for (i = 0; i < sizeof(r6_masks) / sizeof(r6_masks[0]); i++) {
        if ((insn & 0xff000000) == r6_masks[i].cmp_mask)
            return r6_masks[i].reloc_mask;
    }

    set_error_buf(error_buf, error_buf_size,
                  "AOT module load failed: "
                  "unrecognized instruction for 6_X relocation.");
    return 0;
}

static uint32
get_mask_r8(uint32 insn)
{
    if ((0xff000000 & insn) == 0xde000000)
        return 0x00e020e8;
    if ((0xff000000 & insn) == 0x3c000000)
        return 0x0000207f;
    return 0x00001fe0;
}

static uint32
get_mask_r11(uint32 insn)
{
    if (is_duplex(insn))
        return 0x03f00000;
    if ((0xff000000 & insn) == 0xa1000000)
        return 0x060020ff;
    return 0x06003fe0;
}

static uint32
get_mask_r16(uint32 insn, char *error_buf, uint32 error_buf_size)
{
    uint32 i;

    if (is_duplex(insn))
        return 0x03f00000;

    /* Clear end-packet-parse bits for matching */
    insn = insn & ~INST_PARSE_PACKET_END;

    if ((0xff000000 & insn) == 0x48000000)
        return 0x061f20ff;
    if ((0xff000000 & insn) == 0x49000000)
        return 0x061f3fe0;
    if ((0xff000000 & insn) == 0x78000000)
        return 0x00df3fe0;
    if ((0xff000000 & insn) == 0xb0000000)
        return 0x0fe03fe0;

    if ((0xff802000 & insn) == 0x74000000)
        return 0x00001fe0;
    if ((0xff802000 & insn) == 0x74002000)
        return 0x00001fe0;
    if ((0xff802000 & insn) == 0x74800000)
        return 0x00001fe0;
    if ((0xff802000 & insn) == 0x74802000)
        return 0x00001fe0;

    /* Fall back to r6 table */
    for (i = 0; i < sizeof(r6_masks) / sizeof(r6_masks[0]); i++) {
        if ((insn & 0xff000000) == r6_masks[i].cmp_mask)
            return r6_masks[i].reloc_mask;
    }

    set_error_buf(error_buf, error_buf_size,
                  "AOT module load failed: "
                  "unrecognized instruction for 16_X relocation.");
    return 0;
}

/* Scatter bits from 'data' into positions indicated by set bits in 'mask'. */
static uint32
apply_mask(uint32 mask, uint32 data)
{
    uint32 result = 0;
    uint32 off = 0;
    uint32 bit;

    for (bit = 0; bit < 32; bit++) {
        uint32 val_bit = (data >> off) & 1;
        uint32 mask_bit = (mask >> bit) & 1;
        if (mask_bit) {
            result |= (val_bit << bit);
            off++;
        }
    }
    return result;
}

#define PLT_ITEM_SIZE 12

/* Instruction templates with address = 0.
 * The constant extender uses parse bits 0b01 (not-end-of-packet); 0b10 would
 * mark the "{ immext; r28 = ##addr }" packet as :endloop0, so a trampoline
 * entered while a hardware loop is active would decrement LC0 and branch to
 * SA0 instead of jumping.  The r28 transfer and the jumpr each end their
 * packet (parse bits 0b11).  These words match llvm-mc output exactly. */
#define PLT_IMMEXT_TEMPLATE 0x00004000
#define PLT_R28_TEMPLATE 0x7800c01c
#define PLT_JUMPR_R28 0x529cc000

#define MASK_R28_IMM 0x00df3fe0

uint32
get_plt_item_size(void)
{
    return PLT_ITEM_SIZE;
}

uint32
get_plt_table_size(void)
{
    return get_plt_item_size() * (sizeof(target_sym_map) / sizeof(SymbolMap));
}

void
init_plt_table(uint8 *plt)
{
    uint32 i, num = sizeof(target_sym_map) / sizeof(SymbolMap);

    for (i = 0; i < num; i++) {
        uint32 addr = (uint32)(uintptr_t)target_sym_map[i].symbol_addr;
        uint32 *p = (uint32 *)plt;

        p[0] = PLT_IMMEXT_TEMPLATE | apply_mask(MASK_X26, addr >> 6);
        p[1] = PLT_R28_TEMPLATE | apply_mask(MASK_R28_IMM, addr & 0x3F);
        p[2] = PLT_JUMPR_R28;

        plt += PLT_ITEM_SIZE;
    }
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
    switch (reloc_type) {
        case R_HEX_32:
        {
            /* Direct 32-bit relocation: S + A */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) = val;
            break;
        }

        case R_HEX_32_PCREL:
        {
            /* 32-bit PC-relative: S + A - P */
            int32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (int32)((intptr_t)symbol_addr + (intptr_t)reloc_addend
                          - (intptr_t)(target_section_addr + reloc_offset));
            *(int32 *)(target_section_addr + reloc_offset) = val;
            break;
        }

        case R_HEX_B22_PCREL:
        {
            /*
             * 22-bit PC-relative branch: (S + A - P) >> 2
             * 22-bit signed field, word-aligned: +-8MB byte range.
             * If the direct branch is out of range and the symbol has a
             * PLT entry (symbol_index >= 0), fall back to a PLT trampoline,
             * which can reach an arbitrary 32-bit absolute address.
             */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));

            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));

            if (result >= (8 * BH_MB) || result < -(8 * BH_MB)) {
                /* Direct branch out of range: try PLT trampoline. The PLT
                 * entry is a fixed 3-instruction packet that jumps to the
                 * resolved symbol address, so a non-zero addend cannot be
                 * carried through it. */
                if (symbol_index < 0 || reloc_addend != 0) {
                    set_error_buf(error_buf, error_buf_size,
                                  "AOT module load failed: "
                                  "B22_PCREL target out of range.");
                    return false;
                }
                uint8 *plt = (uint8 *)module->code + module->code_size
                             - get_plt_table_size()
                             + get_plt_item_size() * symbol_index;
                result = (intptr_t)((uintptr_t)plt
                                    - (uintptr_t)(target_section_addr
                                                  + reloc_offset));
                if (result >= (8 * BH_MB) || result < -(8 * BH_MB)) {
                    set_error_buf(error_buf, error_buf_size,
                                  "AOT module load failed: "
                                  "B22_PCREL PLT trampoline out of range.");
                    return false;
                }
            }

            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B22, (uint32)(result >> 2));
            break;
        }

        case R_HEX_B15_PCREL:
        {
            /* 15-bit PC-relative branch: (S + A - P) >> 2, +-64KB range */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));

            if (result >= 0x10000 || result < -0x10000) {
                set_error_buf(error_buf, error_buf_size,
                              "AOT module load failed: "
                              "B15_PCREL target out of range.");
                return false;
            }

            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B15, (uint32)(result >> 2));
            break;
        }

        case R_HEX_B13_PCREL:
        {
            /* 13-bit PC-relative branch: (S + A - P) >> 2, +-16KB range */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));

            if (result >= 0x4000 || result < -0x4000) {
                set_error_buf(error_buf, error_buf_size,
                              "AOT module load failed: "
                              "B13_PCREL target out of range.");
                return false;
            }

            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B13, (uint32)(result >> 2));
            break;
        }

        case R_HEX_B9_PCREL:
        {
            /* 9-bit PC-relative branch: (S + A - P) >> 2, +-1KB range */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));

            if (result >= 0x400 || result < -0x400) {
                set_error_buf(error_buf, error_buf_size,
                              "AOT module load failed: "
                              "B9_PCREL target out of range.");
                return false;
            }

            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B9, (uint32)(result >> 2));
            break;
        }

        case R_HEX_B7_PCREL:
        {
            /* 7-bit PC-relative branch: (S + A - P) >> 2, +-256B range */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));

            if (result >= 0x100 || result < -0x100) {
                set_error_buf(error_buf, error_buf_size,
                              "AOT module load failed: "
                              "B7_PCREL target out of range.");
                return false;
            }

            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B7, (uint32)(result >> 2));
            break;
        }

        case R_HEX_LO16:
        {
            /* Low 16 bits of absolute address: (S + A) & 0xFFFF */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_LO16, val & 0xFFFF);
            break;
        }

        case R_HEX_HI16:
        {
            /* High 16 bits of absolute address: (S + A) >> 16 */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_LO16, val >> 16);
            break;
        }

        case R_HEX_B32_PCREL_X:
        {
            /*
             * Extended 32-bit PC-relative for constant extender.
             * Upper 26 bits: (S + A - P) >> 6
             */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_X26, (uint32)(result >> 6));
            break;
        }

        case R_HEX_32_6_X:
        {
            /*
             * Extended 32-bit absolute for constant extender.
             * Upper 26 bits: (S + A) >> 6
             */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_X26, val >> 6);
            break;
        }

        case R_HEX_B22_PCREL_X:
        {
            /*
             * Extended 22-bit PC-relative: low 6 bits of (S + A - P).
             * Paired with R_HEX_B32_PCREL_X on the constant extender.
             */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B22, (uint32)result & 0x3F);
            break;
        }

        case R_HEX_B15_PCREL_X:
        {
            /*
             * Extended 15-bit PC-relative: low 6 bits of (S + A - P).
             * Paired with R_HEX_B32_PCREL_X on the constant extender.
             */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B15, (uint32)result & 0x3F);
            break;
        }

        case R_HEX_B13_PCREL_X:
        {
            /*
             * Extended 13-bit PC-relative: low 6 bits of (S + A - P).
             * Paired with R_HEX_B32_PCREL_X on the constant extender.
             */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B13, (uint32)result & 0x3F);
            break;
        }

        case R_HEX_B9_PCREL_X:
        {
            /*
             * Extended 9-bit PC-relative: low 6 bits of (S + A - P).
             * Paired with R_HEX_B32_PCREL_X on the constant extender.
             */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B9, (uint32)result & 0x3F);
            break;
        }

        case R_HEX_B7_PCREL_X:
        {
            /*
             * Extended 7-bit PC-relative: low 6 bits of (S + A - P).
             * Paired with R_HEX_B32_PCREL_X on the constant extender.
             */
            intptr_t result;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(MASK_B7, (uint32)result & 0x3F);
            break;
        }

        case R_HEX_6_X:
        {
            /* Low 6 bits for constant-extended absolute */
            uint32 val, insn, mask_r6;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            insn = *(uint32 *)(target_section_addr + reloc_offset);
            mask_r6 = get_mask_r6(insn, error_buf, error_buf_size);
            if (!mask_r6)
                return false;
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(mask_r6, val & 0x3F);
            break;
        }

        case R_HEX_6_PCREL_X:
        {
            /* Low 6 bits for constant-extended PC-relative */
            intptr_t result;
            uint32 insn, mask_r6;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            insn = *(uint32 *)(target_section_addr + reloc_offset);
            mask_r6 = get_mask_r6(insn, error_buf, error_buf_size);
            if (!mask_r6)
                return false;
            result =
                (intptr_t)((uintptr_t)symbol_addr + (intptr_t)reloc_addend
                           - (uintptr_t)(target_section_addr + reloc_offset));
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(mask_r6, (uint32)result & 0x3F);
            break;
        }

        case R_HEX_16_X:
        {
            uint32 val, insn, mask_r16;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            insn = *(uint32 *)(target_section_addr + reloc_offset);
            mask_r16 = get_mask_r16(insn, error_buf, error_buf_size);
            if (!mask_r16)
                return false;
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(mask_r16, val & 0x3F);
            break;
        }

        case R_HEX_12_X:
        {
            /* Extended 12-bit absolute: (S + A) with fixed mask */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(0x000007e0, val);
            break;
        }

        case R_HEX_11_X:
        {
            /* Extended 11-bit absolute: low 6 bits of (S + A) */
            uint32 val, insn;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            insn = *(uint32 *)(target_section_addr + reloc_offset);
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(get_mask_r11(insn), val & 0x3F);
            break;
        }

        case R_HEX_10_X:
        {
            /* Extended 10-bit absolute: low 6 bits of (S + A) */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(0x00203fe0, val & 0x3F);
            break;
        }

        case R_HEX_9_X:
        {
            /* Extended 9-bit absolute: low 6 bits of (S + A) */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(0x00003fe0, val & 0x3F);
            break;
        }

        case R_HEX_8_X:
        {
            /* Extended 8-bit absolute: (S + A) */
            uint32 val, insn;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            insn = *(uint32 *)(target_section_addr + reloc_offset);
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(get_mask_r8(insn), val);
            break;
        }

        case R_HEX_7_X:
        {
            /* Extended 7-bit absolute: low 6 bits of (S + A) go in the base
             * instruction; the constant extender carries bits [31:6]. */
            uint32 val;
            CHECK_RELOC_OFFSET(sizeof(uint32));
            val = (uint32)(uintptr_t)symbol_addr + (int32)reloc_addend;
            *(uint32 *)(target_section_addr + reloc_offset) |=
                apply_mask(0x00001f18, val & 0x3F);
            break;
        }

        case R_HEX_16:
        {
            /* Direct 16-bit relocation */
            uint16 val;
            CHECK_RELOC_OFFSET(sizeof(uint16));
            val = (uint16)((uintptr_t)symbol_addr + (int32)reloc_addend);
            *(uint16 *)(target_section_addr + reloc_offset) = val;
            break;
        }

        case R_HEX_8:
        {
            /* Direct 8-bit relocation: (S + A) truncated to 8 bits */
            uint8 val;
            CHECK_RELOC_OFFSET(sizeof(uint8));
            val = (uint8)((uintptr_t)symbol_addr + (int32)reloc_addend);
            *(uint8 *)(target_section_addr + reloc_offset) = val;
            break;
        }

        case R_HEX_NONE:
            break;

        default:
            if (error_buf != NULL)
                snprintf(error_buf, error_buf_size,
                         "Load relocation section failed: "
                         "invalid relocation type %" PRIu32 ".",
                         reloc_type);
            return false;
    }

    return true;
}

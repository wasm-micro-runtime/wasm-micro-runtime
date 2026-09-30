# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import cpp_scope

SRC = """\
void a(void);
#if WASM_ENABLE_GC != 0
void b(void);
#if WASM_ENABLE_THREAD_MGR != 0
void c(void);
#endif
#else
void d(void);
#endif
void e(void);
#ifndef WASM_ENABLE_SIMD
void f(void);
#endif
#if WASM_ENABLE_MEMORY64 == 0
void g(void);
#endif
#if !WASM_ENABLE_TAIL_CALL
void h(void);
#endif
#if WASM_ENABLE_REF_TYPES != 0 || WASM_ENABLE_GC != 0
#if WASM_ENABLE_GC == 0
void i(void);
#endif
#endif
#if WASM_ENABLE_LIBC_WASI != 0 \\
    || WASM_ENABLE_LIB_PTHREAD != 0
void j(void);
#endif
"""

TABLE = cpp_scope.scan(SRC)


def alts(line):
    """The alternatives at `line`, each as a set of (macro, on)."""
    return [set(a) for a in TABLE[line][0]]


def source(line):
    return TABLE[line][1]


def test_outside_any_block():
    assert alts(1) == [set()]
    assert alts(10) == [set()]


def test_single_guard():
    assert alts(3) == [{("WASM_ENABLE_GC", True)}]


def test_nested_guards_are_a_conjunction():
    assert alts(5) == [{("WASM_ENABLE_GC", True),
                        ("WASM_ENABLE_THREAD_MGR", True)}]


def test_else_branch_flips_polarity():
    assert alts(8) == [{("WASM_ENABLE_GC", False)}]


def test_ifndef_is_negated():
    assert alts(12) == [{("WASM_ENABLE_SIMD", False)}]


def test_compare_against_zero_is_the_disabled_path():
    assert alts(15) == [{("WASM_ENABLE_MEMORY64", False)}]


def test_logical_not_is_the_disabled_path():
    assert alts(18) == [{("WASM_ENABLE_TAIL_CALL", False)}]


def test_or_becomes_two_alternatives():
    """`#if A || B` compiles under A *or* B -- not under both together."""
    assert alts(27) == [{("WASM_ENABLE_LIBC_WASI", True)},
                        {("WASM_ENABLE_LIB_PTHREAD", True)}]


def test_continued_directive_is_one_condition():
    """The `\\`-continued second line is part of the same #if."""
    assert source(27) == cpp_scope.PREPROCESSOR


def test_innermost_guard_comes_last():
    """`#if A || B` around `#if A == 0` constrains A twice; the caller
    resolves it by taking the last entry of the tuple."""
    guards = TABLE[22][0]
    assert all(g[-1] == ("WASM_ENABLE_GC", False) for g in guards)


def test_editing_a_directive_points_at_the_macro_on_that_line():
    """Adding `|| WASM_ENABLE_X` to a chain is a change to X, not to the
    whole chain."""
    guards, src = TABLE[25]      # `#if WASM_ENABLE_LIBC_WASI != 0 \\`
    assert src == cpp_scope.MACRO_EDIT
    assert guards == ((("WASM_ENABLE_LIBC_WASI", True),),)

    guards, src = TABLE[26]      # `    || WASM_ENABLE_LIB_PTHREAD != 0`
    assert src == cpp_scope.MACRO_EDIT
    assert guards == ((("WASM_ENABLE_LIB_PTHREAD", True),),)


def test_endif_is_outside_the_block():
    assert alts(9) == [set()]
    assert source(9) == cpp_scope.PREPROCESSOR


def test_unbalanced_endif_does_not_crash():
    cpp_scope.scan("#endif\n#else\nint x;\n")

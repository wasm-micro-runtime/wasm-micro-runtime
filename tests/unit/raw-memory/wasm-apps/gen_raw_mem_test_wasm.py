#!/usr/bin/env python3
# Copyright (C) 2026 Pymergetic | Rouven Raudzus. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Emit raw_mem_test.wasm / raw_mem64_test.wasm fixtures (no wat2wasm)."""

import sys


def uleb(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def section(sec_id: int, payload: bytes) -> bytes:
    return bytes([sec_id]) + uleb(len(payload)) + payload


def export(name: str, kind: int, idx: int) -> bytes:
    nb = name.encode()
    return uleb(len(nb)) + nb + bytes([kind]) + uleb(idx)


def func_body(body: bytes) -> bytes:
    payload = uleb(0) + body
    return uleb(len(payload)) + payload


def build_mem32() -> bytes:
    # types: (i32)->i32, (i32,i32)->, (i32)->i32, (i32,i32,i32)->, (i32,i32,i32)->
    types = (
        uleb(5)
        + bytes([0x60, 1, 0x7F, 1, 0x7F])  # load
        + bytes([0x60, 2, 0x7F, 0x7F, 0])  # store
        + bytes([0x60, 1, 0x7F, 1, 0x7F])  # grow
        + bytes([0x60, 3, 0x7F, 0x7F, 0x7F, 0])  # fill
        + bytes([0x60, 3, 0x7F, 0x7F, 0x7F, 0])  # copy
    )
    funcs = uleb(5) + uleb(0) + uleb(1) + uleb(2) + uleb(3) + uleb(4)
    mems = uleb(1) + bytes([0x00]) + uleb(1)
    exports = (
        uleb(6)
        + export("mem", 2, 0)
        + export("load_i32", 0, 0)
        + export("store_i32", 0, 1)
        + export("memory_grow", 0, 2)
        + export("memory_fill", 0, 3)
        + export("memory_copy", 0, 4)
    )
    load_body = bytes([0x20, 0x00, 0x28, 0x02, 0x00, 0x0B])
    store_body = bytes([0x20, 0x00, 0x20, 0x01, 0x36, 0x02, 0x00, 0x0B])
    grow_body = bytes([0x20, 0x00, 0x40, 0x00, 0x0B])
    # memory.fill: dst, val, len ; opcode 0xFC 0x0B memidx
    fill_body = bytes(
        [0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xFC, 0x0B, 0x00, 0x0B]
    )
    # memory.copy: dst, src, len ; opcode 0xFC 0x0A dst src
    copy_body = bytes(
        [0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xFC, 0x0A, 0x00, 0x00, 0x0B]
    )
    code = (
        uleb(5)
        + func_body(load_body)
        + func_body(store_body)
        + func_body(grow_body)
        + func_body(fill_body)
        + func_body(copy_body)
    )

    mod = bytearray(b"\x00asm\x01\x00\x00\x00")
    mod += section(1, types)
    mod += section(3, funcs)
    mod += section(5, mems)
    mod += section(7, exports)
    mod += section(10, code)
    return bytes(mod)


def build_mem64() -> bytes:
    # i64 address load/store/grow/fill/copy
    types = (
        uleb(5)
        + bytes([0x60, 1, 0x7E, 1, 0x7F])  # load (i64)->i32
        + bytes([0x60, 2, 0x7E, 0x7F, 0])  # store (i64,i32)->
        + bytes([0x60, 1, 0x7E, 1, 0x7E])  # grow (i64)->i64
        + bytes([0x60, 3, 0x7E, 0x7F, 0x7E, 0])  # fill (i64,i32,i64)->
        + bytes([0x60, 3, 0x7E, 0x7E, 0x7E, 0])  # copy (i64,i64,i64)->
    )
    funcs = uleb(5) + uleb(0) + uleb(1) + uleb(2) + uleb(3) + uleb(4)
    # memory64: flags 0x04 (i64 limits), min=1
    mems = uleb(1) + bytes([0x04]) + uleb(1)
    exports = (
        uleb(6)
        + export("mem", 2, 0)
        + export("load_i32", 0, 0)
        + export("store_i32", 0, 1)
        + export("memory_grow", 0, 2)
        + export("memory_fill", 0, 3)
        + export("memory_copy", 0, 4)
    )
    # i64.load32_u offset align - use i32.load with memory64 addr: 0x28
    load_body = bytes([0x20, 0x00, 0x28, 0x02, 0x00, 0x0B])
    store_body = bytes([0x20, 0x00, 0x20, 0x01, 0x36, 0x02, 0x00, 0x0B])
    grow_body = bytes([0x20, 0x00, 0x40, 0x00, 0x0B])
    fill_body = bytes(
        [0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xFC, 0x0B, 0x00, 0x0B]
    )
    copy_body = bytes(
        [0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xFC, 0x0A, 0x00, 0x00, 0x0B]
    )
    code = (
        uleb(5)
        + func_body(load_body)
        + func_body(store_body)
        + func_body(grow_body)
        + func_body(fill_body)
        + func_body(copy_body)
    )

    mod = bytearray(b"\x00asm\x01\x00\x00\x00")
    mod += section(1, types)
    mod += section(3, funcs)
    mod += section(5, mems)
    mod += section(7, exports)
    mod += section(10, code)
    return bytes(mod)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(
            f"usage: {sys.argv[0]} <out.wasm> [--mem64]",
            file=sys.stderr,
        )
        return 2
    mem64 = len(sys.argv) == 3 and sys.argv[2] == "--mem64"
    data = build_mem64() if mem64 else build_mem32()
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

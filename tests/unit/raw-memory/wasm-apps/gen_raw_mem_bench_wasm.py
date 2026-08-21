#!/usr/bin/env python3
# Copyright (C) 2026 Pymergetic | Rouven Raudzus. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Emit raw_mem_bench.wasm for Raw vs sandbox microbenches."""

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


def name_bytes(s: str) -> bytes:
    b = s.encode()
    return uleb(len(b)) + b


def func_body(locals_payload: bytes, body: bytes) -> bytes:
    payload = locals_payload + body
    return uleb(len(payload)) + payload


def loop_load_body(with_host_call: bool) -> bytes:
    """for i in 0..n: acc += load_or_host(base+i*4)"""
    # after computing addr on stack:
    if with_host_call:
        # call import 0 (host_ldadd)
        access = bytes([0x10, 0x00])
    else:
        access = bytes([0x28, 0x02, 0x00])  # i32.load
    return bytes(
        [
            0x02,
            0x40,
            0x03,
            0x40,
            0x20,
            0x02,
            0x20,
            0x01,
            0x4F,
            0x0D,
            0x01,
            0x20,
            0x03,
            0x20,
            0x00,
            0x20,
            0x02,
            0x41,
            0x02,
            0x74,
            0x6A,
        ]
    ) + access + bytes(
        [
            0x6A,
            0x21,
            0x03,
            0x20,
            0x02,
            0x41,
            0x01,
            0x6A,
            0x21,
            0x02,
            0x0C,
            0x00,
            0x0B,
            0x0B,
            0x20,
            0x03,
            0x0B,
        ]
    )


def touch_body() -> bytes:
    return bytes(
        [
            0x02,
            0x40,
            0x03,
            0x40,
            0x20,
            0x02,
            0x20,
            0x01,
            0x4F,
            0x0D,
            0x01,
            0x20,
            0x00,
            0x20,
            0x02,
            0x41,
            0x02,
            0x74,
            0x6A,
            0x28,
            0x02,
            0x00,  # v
            0x20,
            0x00,
            0x20,
            0x02,
            0x41,
            0x02,
            0x74,
            0x6A,
            0x20,
            0x00,
            0x20,
            0x02,
            0x41,
            0x02,
            0x74,
            0x6A,
            0x28,
            0x02,
            0x00,
            0x41,
            0x01,
            0x6A,
            0x36,
            0x02,
            0x00,
            0x20,
            0x03,
            0x6A,
            0x21,
            0x03,
            0x20,
            0x02,
            0x41,
            0x01,
            0x6A,
            0x21,
            0x02,
            0x0C,
            0x00,
            0x0B,
            0x0B,
            0x20,
            0x03,
            0x0B,
        ]
    )


def build() -> bytes:
    # type0: (i32)->i32  host
    # type1: (i32,i32)->i32  benches
    types = (
        uleb(2)
        + bytes([0x60, 1, 0x7F, 1, 0x7F])
        + bytes([0x60, 2, 0x7F, 0x7F, 1, 0x7F])
    )
    imports = (
        uleb(1)
        + name_bytes("env")
        + name_bytes("host_ldadd")
        + bytes([0x00])
        + uleb(0)
    )
    # defined: sum=1, touch=2, host_churn=3  (import takes func idx 0)
    funcs = uleb(3) + uleb(1) + uleb(1) + uleb(1)
    mems = uleb(1) + bytes([0x00]) + uleb(4)
    exports = (
        uleb(4)
        + export("mem", 2, 0)
        + export("sum_i32", 0, 1)
        + export("touch_i32", 0, 2)
        + export("host_churn", 0, 3)
    )
    locs = bytes([0x01, 0x02, 0x7F])
    code = (
        uleb(3)
        + func_body(locs, loop_load_body(False))
        + func_body(locs, touch_body())
        + func_body(locs, loop_load_body(True))
    )

    mod = bytearray(b"\x00asm\x01\x00\x00\x00")
    mod += section(1, types)
    mod += section(2, imports)
    mod += section(3, funcs)
    mod += section(5, mems)
    mod += section(7, exports)
    mod += section(10, code)
    return bytes(mod)


def main() -> None:
    out = sys.argv[1] if len(sys.argv) > 1 else "raw_mem_bench.wasm"
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")


if __name__ == "__main__":
    main()

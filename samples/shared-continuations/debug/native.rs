// Copyright (C) 2026 Matt Hargett. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
unsafe extern "C" {
    fn native_bridge(value: i32) -> i32;
}
#[unsafe(no_mangle)]
#[inline(never)]
pub extern "C" fn native_rust(value: i32) -> i32 {
    std::hint::black_box(unsafe { native_bridge(value) }) + 1
}

#[cfg(diagnostic_errors)]
#[unsafe(no_mangle)]
#[inline(never)]
pub extern "C" fn native_rust_error() -> i32 {
    let result: Result<(), i32> = Err(-12);
    result.err().unwrap_or(0)
}

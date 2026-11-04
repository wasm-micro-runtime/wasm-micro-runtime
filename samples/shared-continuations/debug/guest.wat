;; Copyright (C) 2026 Matt Hargett. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
(module $stack_audit
  (import "example:native" "call" (func $native_call (param i32) (result i32)))
  (func $leaf (param i32) (result i32)
    local.get 0 i32.eqz if unreachable end
    local.get 0 call $native_call drop unreachable)
  (func $middle (param i32) (result i32) local.get 0 call $leaf i32.const 1 i32.add)
  (func $root (export "run") (param i32) (result i32) local.get 0 call $middle i32.const 1 i32.add))

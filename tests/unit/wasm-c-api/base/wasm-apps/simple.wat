;; Copyright (C) 2026 Intel Corporation.  All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; The one fixture of wasm-c-api/base: it has every shape the cases inspect.
;;  - one function import, so an instance needs the caller to supply it
;;  - exported function / global / table / memory
;;  - an exported function that traps, for the trap and frame cases
(module
  (import "env" "imported" (func $imported (param i32) (result i32)))

  (global $counter (export "counter") (mut i32) (i32.const 7))

  (memory (export "memory") 1 2)

  (table (export "table") 2 4 funcref)

  (func $placeholder (result i32)
    i32.const 0)

  (elem (i32.const 0) $placeholder $add)

  (func $add (export "add") (param $lhs i32) (param $rhs i32) (result i32)
    local.get $lhs
    local.get $rhs
    i32.add)

  (func (export "trap")
    unreachable)

  (func (export "call_imported") (param i32) (result i32)
    local.get 0
    call $imported))

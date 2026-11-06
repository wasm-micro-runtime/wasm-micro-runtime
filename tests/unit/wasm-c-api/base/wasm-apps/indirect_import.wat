;; Copyright (C) 2026 Matt Hargett. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
(module
  (type $host (func (param i32) (result i32)))
  (import "env" "plain" (func $plain (type $host)))
  (import "env" "context" (func $context (type $host)))
  (table 2 funcref)
  (elem (i32.const 0) func $plain $context)
  (func (export "call") (param $index i32) (param $value i32) (result i32)
    local.get $value local.get $index call_indirect (type $host)))

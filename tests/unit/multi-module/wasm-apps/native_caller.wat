;; Copyright (C) 2026 WAMR Community.  All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

(module
  ;; a native function, which calls a function of another module instance
  (import "env" "call_trap" (func $call_trap (param i32) (result i32)))

  (func (export "call_native") (param i32) (result i32)
    (call $call_trap (local.get 0))))

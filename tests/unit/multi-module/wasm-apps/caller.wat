;; Copyright (C) 2026 WAMR Community.  All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

(module
  (import "callee" "trap" (func $trap (param i32) (result i32)))

  ;; the imported function, exported as it is
  (export "reexported_trap" (func $trap))

  ;; the imported function, called by a function of this module
  (func (export "call_trap") (param i32) (result i32)
    (call $trap (local.get 0))))

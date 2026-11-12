;; Copyright (C) 2026 WAMR Community.  All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

(module
  (func $do_trap (param i32) (result i32)
    (unreachable))

  (func (export "trap") (param i32) (result i32)
    (call $do_trap (local.get 0))))

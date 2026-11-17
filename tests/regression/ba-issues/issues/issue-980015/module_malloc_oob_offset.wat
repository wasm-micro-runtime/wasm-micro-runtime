;; Copyright (C) 2026 Intel Corporation. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; The module exports malloc/free, so the runtime routes its own allocations
;; (here: the libc-builtin strdup) through the guest malloc. This malloc
;; returns the offset stored at address 0 without looking at the size.
;; to_test stores an offset 6 bytes before the end of linear memory, then
;; calls strdup on a 17-byte string. The runtime must range check the returned
;; offset and raise an exception instead of copying past linear memory.
;;
;; Expected: "Exception: allocate memory failed", exit code 1.
(module
  (import "env" "strdup" (func $strdup (param i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 16) "AAAAAAAAAAAAAAAA\00")

  (func (export "malloc") (param i32) (result i32)
    (i32.load (i32.const 0)))

  (func (export "free") (param i32)
    nop)

  (func (export "to_test") (result i32)
    (i32.store (i32.const 0) (i32.const 65530))
    (call $strdup (i32.const 16)))
)

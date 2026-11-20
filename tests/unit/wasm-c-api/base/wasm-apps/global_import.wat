;; Copyright (C) 2026 Intel Corporation.  All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; A module whose only import is a global, so an instance needs the caller to
;; link that global, and whose export reads it back.
(module
  (import "env" "g" (global $g i32))

  (func (export "read") (result i32)
    global.get $g))

;; Copyright (C) 2026 Intel Corporation. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; Thin wrappers around the WASI socket calls that take an address, so that
;; the host test can drive them with hand-built guest memory.
(module
  (import "wasi_snapshot_preview1" "sock_open"
    (func $sock_open (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_bind"
    (func $sock_bind (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_connect"
    (func $sock_connect (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_send_to"
    (func $sock_send_to (param i32 i32 i32 i32 i32 i32) (result i32)))

  (memory (export "memory") 1)

  (func (export "call_sock_open") (param i32 i32 i32 i32) (result i32)
    (call $sock_open (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "call_sock_bind") (param i32 i32) (result i32)
    (call $sock_bind (local.get 0) (local.get 1)))
  (func (export "call_sock_connect") (param i32 i32) (result i32)
    (call $sock_connect (local.get 0) (local.get 1)))
  (func (export "call_sock_send_to") (param i32 i32 i32 i32 i32 i32) (result i32)
    (call $sock_send_to (local.get 0) (local.get 1) (local.get 2)
                        (local.get 3) (local.get 4) (local.get 5)))
)

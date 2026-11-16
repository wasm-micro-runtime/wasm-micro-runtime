;; Copyright (C) 2026 Intel Corporation. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; Thin wrappers around the WASI calls that take an iovec array, so that the
;; host test can drive them with hand-built guest memory.
(module
  (import "wasi_snapshot_preview1" "fd_read"
    (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_pread"
    (func $fd_pread (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_pwrite"
    (func $fd_pwrite (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_send"
    (func $sock_send (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_recv"
    (func $sock_recv (param i32 i32 i32 i32 i32 i32) (result i32)))

  (memory (export "memory") 1)

  (func (export "call_fd_read") (param i32 i32 i32 i32) (result i32)
    (call $fd_read (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "call_fd_write") (param i32 i32 i32 i32) (result i32)
    (call $fd_write (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "call_fd_pread") (param i32 i32 i32 i64 i32) (result i32)
    (call $fd_pread (local.get 0) (local.get 1) (local.get 2)
                    (local.get 3) (local.get 4)))
  (func (export "call_fd_pwrite") (param i32 i32 i32 i64 i32) (result i32)
    (call $fd_pwrite (local.get 0) (local.get 1) (local.get 2)
                     (local.get 3) (local.get 4)))
  (func (export "call_sock_send") (param i32 i32 i32 i32 i32) (result i32)
    (call $sock_send (local.get 0) (local.get 1) (local.get 2)
                     (local.get 3) (local.get 4)))
  (func (export "call_sock_recv") (param i32 i32 i32 i32 i32 i32) (result i32)
    (call $sock_recv (local.get 0) (local.get 1) (local.get 2)
                     (local.get 3) (local.get 4) (local.get 5)))
)

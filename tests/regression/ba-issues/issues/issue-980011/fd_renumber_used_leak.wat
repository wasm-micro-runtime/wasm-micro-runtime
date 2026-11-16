;; Defect B: each successful fd_renumber() must remove
;; exactly one descriptor from the table's used count.
;; Before the fix, the count underflows and later fd allocation hangs.
(module
  (import "wasi_snapshot_preview1" "sock_open"
    (func $sock_open (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_renumber"
    (func $fd_renumber (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit"
    (func $proc_exit (param i32)))
  (memory (export "memory") 1)
  (global $keeper (mut i32) (i32.const 0))
  ;; sock_open(poolfd=0 (ignored), af=INET4, socktype=STREAM, &fd) -> fd
  (func $open (result i32)
    (drop (call $sock_open (i32.const 0) (i32.const 0) (i32.const 1) (i32.const 0)))
    (i32.load (i32.const 0)))
  (func (export "_start")
    (local $i i32)
    (global.set $keeper (call $open))
    ;; each round: +1 fd, then -1 fd via renumber onto the keeper.
    (loop $drain
      (drop (call $fd_renumber (call $open) (global.get $keeper)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $drain (i32.lt_s (local.get $i) (i32.const 12))))
    ;; the table must still hand out new fds.
    (local.set $i (i32.const 0))
    (loop $ins
      (drop (call $open))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $ins (i32.lt_s (local.get $i) (i32.const 64))))))

;; Defect A: fd_renumber(fd, fd) must succeed.
;; Before the fix, the runtime dereferences NULL after detaching the fd.
(module
  (import "wasi_snapshot_preview1" "fd_renumber"
    (func $fd_renumber (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit"
    (func $proc_exit (param i32)))
  (memory (export "memory") 1)
  (func (export "_start")
    ;; stdin onto itself must return ESUCCESS (0). Exit non-zero otherwise;
    ;; a normal return is exit code 0 (proc_exit(0) raises an exception here).
    (if (i32.ne (call $fd_renumber (i32.const 0) (i32.const 0)) (i32.const 0))
      (then (call $proc_exit (i32.const 1))))))

;; fd_readdir() on a character device fd must fail with ENOTDIR (54).
;; Before the fix it reaches the directory stream code.
;; The exit code of the module is the errno returned by fd_readdir().
;; Requires --dir=/dev so that "null" is reachable under the preopen at fd 3.
(module
  (import "wasi_snapshot_preview1" "path_open"
    (func $path_open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_readdir"
    (func $fd_readdir (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit"
    (func $proc_exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 16) "null")
  (func (export "_start")
    ;; open "null" under the preopen (fd 3) with FD_READDIR (1 << 14);
    ;; the new fd is stored at address 1000.
    (drop (call $path_open
      (i32.const 3) (i32.const 0) (i32.const 16) (i32.const 4)
      (i32.const 0) (i64.const 16384) (i64.const 0) (i32.const 0)
      (i32.const 1000)))
    ;; exit with the errno of fd_readdir on the character device.
    (call $proc_exit
      (call $fd_readdir (i32.load (i32.const 1000)) (i32.const 2000)
                        (i32.const 256) (i64.const 0) (i32.const 3000)))))

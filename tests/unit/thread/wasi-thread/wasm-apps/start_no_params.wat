(module
  (import "wasi" "thread-spawn" (func $spawn (param i32) (result i32)))
  (memory (export "memory") 1 1 shared)

  ;; () with no result
  (func (export "wasi_thread_start"))

  ;; the spawn result; -1 means the module was rejected at spawn
  (func (export "spawn") (result i32)
    (call $spawn (i32.const 0)))
  ;; 1 once a spawned wasi_thread_start has run
  (func (export "flag") (result i32)
    (i32.load (i32.const 64)))
)

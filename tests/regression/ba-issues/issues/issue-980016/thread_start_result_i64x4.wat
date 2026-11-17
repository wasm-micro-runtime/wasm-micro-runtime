(module
  (import "wasi" "thread-spawn" (func $spawn (param i32) (result i32)))
  (memory (export "memory") 1 1 shared)

  ;; wasi-threads mandates (param i32 i32) with no results.
  ;; Four i64 results are 8 cells, written back into the 2-cell host argv.
  (func (export "wasi_thread_start") (param i32 i32)
                                     (result i64 i64 i64 i64)
    i64.const 0x4141414141414141
    i64.const 0x4242424242424242
    i64.const 0x4343434343434343
    i64.const 0x4444444444444444)

  ;; The spawn must be rejected (-1). An accepted spawn traps.
  (func (export "_start")
    (if (i32.ne (call $spawn (i32.const 0)) (i32.const -1))
      (then unreachable)))
)

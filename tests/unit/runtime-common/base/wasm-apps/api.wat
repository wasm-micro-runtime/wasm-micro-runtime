;; Fixture of the wasm_runtime_common.c public API cases: one import, one of
;; every export kind, a table with two populated slots, a function that calls
;; the import so the case can observe the native call environment, two imports
;; that are registered as raw natives, and one that
;; grows the memory (a module that never grows keeps its maximum at the initial
;; size when the runtime is built with SHRUNK_MEMORY).
(module
  (import "env" "host_add" (func $host_add (param i32 i32) (result i32)))
  (import "env" "host_raw" (func $host_raw (param i32 i32) (result i32)))
  (import "env" "host_raw_ptr" (func $host_raw_ptr (param i32) (result i32)))

  (memory (export "mem") 1 3)
  (data (i32.const 16) "\2a\00\00\00")
  (table (export "tbl") 4 8 funcref)
  (global (export "g_mut") (mut i32) (i32.const 7))
  (global (export "g_const") i64 (i64.const 9))

  (func $add (export "add") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add
  )

  (func (export "mix") (param i32 i64 f32 f64) (result f64)
    local.get 3
  )

  (func $noop (export "noop"))

  (func (export "trap")
    unreachable
  )

  (func (export "grow") (param i32) (result i32)
    local.get 0
    memory.grow
  )

  (func (export "call_host") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    call $host_add
  )

  ;; the raw natives take their arguments as an array of 64-bit slots
  (func (export "call_raw") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    call $host_raw
  )

  ;; host_raw_ptr's parameter is a pointer into the linear memory
  (func (export "call_raw_ptr") (param i32) (result i32)
    local.get 0
    call $host_raw_ptr
  )

  (elem (i32.const 0) $add $noop)
)

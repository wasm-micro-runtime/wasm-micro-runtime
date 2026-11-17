(module
  (import "env" "pthread_create"
    (func $pthread_create (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 2)
  (table 1 funcref)
  ;; aux stack info, as clang emits it, so pthread_create can allocate a stack
  (global (export "__data_end") i32 (i32.const 1024))
  (global (export "__heap_base") i32 (i32.const 65536))
  (global (export "__stack_pointer") (mut i32) (i32.const 65536))
  (elem (i32.const 0) $routine)

  ;; Three i32 results: 12 bytes written back into the 1-cell host argv
  (func $routine (param i32) (result i32 i32 i32)
    i32.const 1
    i32.const 2
    i32.const 3)

  ;; pthread_create must be rejected (-1). An accepted spawn traps.
  (func (export "_start")
    (if (i32.ne (call $pthread_create (i32.const 16) (i32.const 0)
                                      (i32.const 0) (i32.const 0))
                (i32.const -1))
      (then unreachable)))
)

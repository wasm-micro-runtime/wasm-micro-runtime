(module
  (import "env" "pthread_create"
    (func $pthread_create (param i32 i32 i32 i32) (result i32)))
  (import "env" "pthread_join"
    (func $pthread_join (param i32 i32) (result i32)))
  (memory (export "memory") 2)
  (table 1 funcref)
  ;; aux stack info, as clang emits it, so pthread_create can allocate a stack
  (global (export "__data_end") i32 (i32.const 1024))
  (global (export "__heap_base") i32 (i32.const 65536))
  (global (export "__stack_pointer") (mut i32) (i32.const 65536))
  (elem (i32.const 0) $routine)

  ;; routine: (param i32) with an f32 result
  (func $routine (param i32) (result f32) f32.const 0)

  ;; $elem is the table index passed to pthread_create; the thread id is
  ;; stored at offset 16
  (func (export "spawn") (param $elem i32) (result i32)
    (call $pthread_create (i32.const 16) (i32.const 0) (local.get $elem)
                          (i32.const 0)))
  (func (export "join") (result i32)
    (call $pthread_join (i32.load (i32.const 16)) (i32.const 0)))
)

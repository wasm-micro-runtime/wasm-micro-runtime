(module
  (import "env" "pthread_create"
    (func $pthread_create (param i32 i32 i32 i32) (result i32)))
  (import "env" "pthread_join"
    (func $pthread_join (param i32 i32) (result i32)))
  (import "env" "pthread_key_create"
    (func $pthread_key_create (param i32 i32) (result i32)))
  (import "env" "pthread_setspecific"
    (func $pthread_setspecific (param i32 i32) (result i32)))
  ;; shared, so the worker's stores are visible to the main thread
  (memory (export "memory") 2 2 shared)
  (table 3 funcref)
  ;; aux stack info, as clang emits it, so pthread_create can allocate a stack
  (global (export "__data_end") i32 (i32.const 1024))
  (global (export "__heap_base") i32 (i32.const 65536))
  (global (export "__stack_pointer") (mut i32) (i32.const 65536))
  (elem (i32.const 0) $routine $destructor $replacement)

  ;; worker: register $destructor (table index 1) as a valid destructor, set a
  ;; nonzero key value, then optionally replace table slot 1 before returning
  (func $routine (param i32) (result i32)
    (drop (call $pthread_key_create (i32.const 32) (i32.const 1)))
    (drop (call $pthread_setspecific (i32.load (i32.const 32)) (i32.const 1)))
    (if (i32.load (i32.const 72))
      (then (table.set (i32.const 1) (table.get (i32.const 2)))))
    (i32.const 0))

  ;; valid destructor: (param i32) with no results
  (func $destructor (param i32)
    (i32.store (i32.const 64) (i32.const 1)))

  ;; (param i32) with two i64 results: a call that copies its results into the
  ;; destructor's 1-cell argv overflows the host stack
  (func $replacement (param i32) (result i64 i64)
    (i32.store (i32.const 68) (i32.const 1))
    (i64.const 1)
    (i64.const 2))

  ;; $replace != 0 makes the worker replace the destructor slot; the thread id
  ;; is stored at offset 16
  (func (export "spawn") (param $replace i32) (result i32)
    (i32.store (i32.const 72) (local.get $replace))
    (call $pthread_create (i32.const 16) (i32.const 0) (i32.const 0)
                          (i32.const 0)))
  (func (export "join") (result i32)
    (call $pthread_join (i32.load (i32.const 16)) (i32.const 0)))
  (func (export "destructor_ran") (result i32) (i32.load (i32.const 64)))
  (func (export "replacement_ran") (result i32) (i32.load (i32.const 68)))
)

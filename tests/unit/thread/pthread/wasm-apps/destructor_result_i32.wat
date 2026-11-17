(module
  (import "env" "pthread_key_create"
    (func $pthread_key_create (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (table 2 funcref)
  (elem (i32.const 1) $destructor)

  ;; destructor: declares a result, POSIX destructors return void
  (func $destructor (param i32) (result i32) local.get 0)

  (func (export "create_key") (param $elem i32) (result i32)
    (call $pthread_key_create (i32.const 16) (local.get $elem)))
)

(module
  (import "env" "pthread_key_create"
    (func $pthread_key_create (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (table 1 funcref)
  (elem (i32.const 0) $destructor)

  (func $destructor (param i32))

  ;; create_key(1) passes an index past the end of the table
  (func (export "create_key") (param $elem i32) (result i32)
    (call $pthread_key_create (i32.const 16) (local.get $elem)))
)

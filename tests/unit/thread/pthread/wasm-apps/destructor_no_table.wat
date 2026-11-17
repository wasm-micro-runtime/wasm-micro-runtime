(module
  (import "env" "pthread_key_create"
    (func $pthread_key_create (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  ;; no table is declared, so any nonzero destructor index is invalid

  ;; create_key(1) passes a nonzero index with no table to look up
  (func (export "create_key") (param $elem i32) (result i32)
    (call $pthread_key_create (i32.const 16) (local.get $elem)))
)

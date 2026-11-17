(module
  (import "env" "pthread_key_create"
    (func $pthread_key_create (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (table 2 funcref)
  (elem (i32.const 1) $destructor)

  ;; destructor: signature-correct (param i32) with no results
  (func $destructor (param i32))

  ;; $elem is the destructor's table index; the key is stored at offset 16
  (func (export "create_key") (param $elem i32) (result i32)
    (call $pthread_key_create (i32.const 16) (local.get $elem)))
)

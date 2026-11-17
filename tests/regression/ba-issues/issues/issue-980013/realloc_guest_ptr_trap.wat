;; Regression case for libc-builtin realloc(): a guest pointer that is out of
;; linear memory must be rejected by the (*i)i signature validation.
;;
;; Expected: trap "out of bounds memory access".
(module
  (import "env" "realloc" (func $realloc (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  (func (export "to_test") (result i32)
    (local $end i32)
    ;; one past the last byte of linear memory
    (local.set $end (i32.mul (memory.size) (i32.const 65536)))
    (drop (call $realloc (local.get $end) (i32.const 16)))
    (i32.const 0)))

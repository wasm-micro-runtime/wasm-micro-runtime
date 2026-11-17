;; Regression test for the libc-builtin strtol endptr write.
;;
;; strtol writes a full host `char *` (8 bytes on 64-bit) through endptr,
;; while the wrapper validates only the 4-byte guest slot. Here the slot is the
;; last 4 bytes of linear memory: the validation passes, and an unfixed wrapper
;; writes 4 more bytes past the end of linear memory. The fixed wrapper stores
;; only the 4-byte guest offset. Run with --heap-size=0 so that no app heap
;; extends the memory.
;;
;; Expected: returns 100 (0x64:i32).
(module
  (import "env" "strtol" (func $strtol (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

  ;; "100" at offset 100.
  (data (i32.const 100) "100\00")

  (func (export "to_test") (result i32)
    (call $strtol (i32.const 100) (i32.const 65532) (i32.const 10)))
)

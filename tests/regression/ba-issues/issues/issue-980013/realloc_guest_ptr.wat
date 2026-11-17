;; Regression case for libc-builtin realloc() with a guest pointer.
;;
;; realloc is registered as (*i)i: the guest pointer is validated and
;; converted by the runtime, and the module heap layer only accepts blocks
;; that live in the app heap.
;;
;; Returns four status codes: 0 means the corresponding check passed.
;; Expected: 0x0:i32,0x0:i32,0x0:i32,0x0:i32
(module
  (import "env" "realloc" (func $realloc (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  (func (export "to_test") (result i32 i32 i32 i32)
    (local $p i32)
    (local $r1 i32)
    (local $r2 i32)
    (local $r3 i32)
    ;; realloc(NULL, 16) behaves like malloc(16)
    (local.set $p (call $realloc (i32.const 0) (i32.const 16)))
    (local.set $r1 (i32.eqz (local.get $p)))
    ;; grow the block
    (local.set $p (call $realloc (local.get $p) (i32.const 32)))
    (local.set $r2 (i32.eqz (local.get $p)))
    ;; offset 1 is inside linear memory but outside the app heap: the module
    ;; heap layer must reject it without touching the allocator
    (local.set $r3 (call $realloc (i32.const 1) (i32.const 16)))
    (local.get $r1) (local.get $r2) (local.get $r3) (i32.const 0)))

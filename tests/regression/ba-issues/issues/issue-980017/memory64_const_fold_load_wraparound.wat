;; Memory64 constant-fold bounds check wraps in uint64.
;; The effective address 0xFFFFFFFFFFFFFFF0 + 0x1F = 2^64 + 15 is out of the
;; i64 address space, so the store must trap. The AOT compiler computes
;; value + offset in uint64, wraps to 0xF and skips the bounds check.
(module
  (memory (export "memory") i64 1)
  (func (export "_start")
    i64.const 0xFFFFFFFFFFFFFFF0
    i64.const 42
    i64.store offset=0x1F
  )
)

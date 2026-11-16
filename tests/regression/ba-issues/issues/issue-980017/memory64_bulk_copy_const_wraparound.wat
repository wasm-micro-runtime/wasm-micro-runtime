;; Memory64 constant-fold bounds check in bulk memory ops wraps in uint64.
;; src + len = 0xFFFFFFFFFFFFFE00 + 0x200 wraps
;; to 0, so the check passes and the AOT compiler emits a GEP to mem_base - 0x200.
;; The source range is out of the i64 address space, so memory.copy must trap.
(module
  (memory (export "memory") i64 1)
  (func (export "_start")
    i64.const 1
    i64.const 0xFFFFFFFFFFFFFE00
    i64.const 512
    memory.copy
  )
)

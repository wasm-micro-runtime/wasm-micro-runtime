;; Memory64 dynamic bulk memory check wraps in uint64.
;; src is read from memory at runtime, so the constant-fold path is not used.
;; src = 0xFFFFFFFFFFFFFE00 and len = 0x200 wrap to 0, which the previous dynamic
;; check accepted when max_addr was 0. memory.copy must trap.
(module
  (memory (export "memory") i64 1)
  (data (i64.const 0) "\00\fe\ff\ff\ff\ff\ff\ff")
  (func (export "_start")
    i64.const 8        ;; dst
    i64.const 0
    i64.load           ;; src = 0xFFFFFFFFFFFFFE00, runtime value
    i64.const 512      ;; len
    memory.copy
  )
)

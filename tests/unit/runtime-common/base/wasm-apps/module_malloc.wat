;; Exports malloc/free, so the runtime disables its app heap and routes
;; wasm_runtime_module_malloc() through the guest malloc.
;; malloc returns its argument as the offset, so the test controls the offset.
;; free stores its argument at address 0, so the test can observe whether the
;; guest free was called.
(module
  (memory (export "memory") 1)

  (func (export "malloc") (param i32) (result i32)
    local.get 0
  )

  (func (export "free") (param i32)
    i32.const 0
    local.get 0
    i32.store
  )
)

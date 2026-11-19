;; Exports malloc/free, so the runtime disables its app heap and routes
;; wasm_runtime_module_malloc() through the guest malloc.
;; malloc ignores its size and returns the word stored at address 0, so the
;; test sets the offset directly, independent of the requested size.
;; When the word at address 4 is non-zero, malloc grows the memory by one page
;; before it returns, so the offset may lie in a page that did not exist when
;; the call started.
(module
  (memory (export "memory") 1)

  (func (export "malloc") (param i32) (result i32)
    i32.const 4
    i32.load
    if
      i32.const 1
      memory.grow
      drop
    end
    i32.const 0
    i32.load
  )

  (func (export "free") (param i32)
    nop
  )
)

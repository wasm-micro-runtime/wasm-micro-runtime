;; One exported linear memory: 1 page initially, up to 65536 pages (4 GiB).
;; memory.grow is present so the loader does not shrink the memory to its
;; initial size (WASM_ENABLE_SHRUNK_MEMORY).
(module
  (memory (export "memory") 1 65536)
  (func (export "grow") (param i32) (result i32)
    local.get 0
    memory.grow
  )
)

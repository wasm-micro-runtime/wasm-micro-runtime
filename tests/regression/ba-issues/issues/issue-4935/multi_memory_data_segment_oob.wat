;; Regression test for https://github.com/wasm-micro-runtime/wasm-micro-runtime/issues/4935
;;
;; A module that imports a memory and also defines a local memory, then
;; initializes an active data segment targeting the *local* memory by
;; explicit index (index 1). With the Memory64 proposal enabled, the loader
;; used to resolve the memory's 64-bit flag with
;; `if (module->import_memory_count > 0)` instead of
;; `if (mem_index < module->import_memory_count)`, so a data segment index
;; that pointed at the local memory was still routed into the imports array,
;; reading past its allocation (heap-buffer-overflow). This module is valid
;; and must load successfully.
(module
  (import "env" "m" (memory 1))
  (memory 1)
  (data (memory 1) (i32.const 0) "")
)

;; Hand-assembled to table_anyref_i31.wasm, see the note in CMakeLists.txt.
;; Table 0 is an anyref table, init() stores a non-null i31ref in element 1
(module
  (table 2 anyref)
  (func (export "init")
    (table.set (i32.const 1) (ref.i31 (i32.const 0))))
)

;; Hand-assembled to table_funcref.wasm, see the note in CMakeLists.txt.
;; Table 0 is a funcref table whose element 1 is $f
(module
  (table 2 funcref)
  (elem (i32.const 1) $f)
  (func $f)
)

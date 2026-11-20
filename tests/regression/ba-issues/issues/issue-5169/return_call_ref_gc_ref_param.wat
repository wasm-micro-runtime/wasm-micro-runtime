;; return_call_ref with a GC reference argument
;; https://github.com/wasm-micro-runtime/wasm-micro-runtime/issues/5169
(module
  (type $cell (struct (field (mut i32))))
  (type $set (func (param (ref $cell) i64 (ref null $cell)) (result i32)))
  (elem declare func $set)
  (func $set (type $set)
    (struct.set $cell 0 (local.get 0) (i32.wrap_i64 (local.get 1)))
    (i32.add (struct.get $cell 0 (local.get 0))
             (ref.is_null (local.get 2))))
  (func $bounce (param (ref $cell) (ref $set)) (result i32)
    (return_call_ref $set
      (local.get 0) (i64.const 1146) (ref.null $cell) (local.get 1)))
  (func $bounce_direct (param (ref $cell)) (result i32)
    (return_call $set (local.get 0) (i64.const 2000) (local.get 0)))
  (func (export "f") (result i32)
    (local $x (ref $cell))
    (local.set $x (struct.new $cell (i32.const 146)))
    (i32.add
      (i32.add (call $bounce (local.get $x) (ref.func $set))
               (call $bounce_direct (local.get $x)))
      (struct.get $cell 0 (local.get $x)))))

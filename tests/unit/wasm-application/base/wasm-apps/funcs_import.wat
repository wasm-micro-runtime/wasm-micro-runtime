(module
  (import "env" "host_fn" (func $host_fn (param i32) (result i32)))
  (func (export "call_import") (param i32) (result i32)
    (call $host_fn (local.get 0))))

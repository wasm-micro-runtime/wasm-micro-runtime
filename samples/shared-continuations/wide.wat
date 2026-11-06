(module (import "host" "wide" (func $wide (param i64) (result i64)))
 (func (export "run") (param i32 i64) (result i64) local.get 1 call $wide i64.const 1 i64.add))

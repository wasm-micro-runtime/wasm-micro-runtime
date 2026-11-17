(module
 (import "h" "pause" (func $pause (param i32)))
 (memory (export "memory") 1 3)
 (func (export "run") (param $seed i32) (result i32)
  i32.const 0 local.get $seed i32.store
  i32.const 0 call $pause
  i32.const 1 memory.grow drop
  i32.const 65536 local.get $seed i32.const 1 i32.add i32.store
  i32.const 1 call $pause
  i32.const 0 i32.load i32.const 65536 i32.load i32.add)
 (func (export "void") i32.const 9 call $pause)
 (func (export "trap") i32.const 8 call $pause unreachable))

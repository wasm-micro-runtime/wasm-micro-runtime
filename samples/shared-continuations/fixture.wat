(module
 (type $i2i (func (param i32) (result i32)))
 (import "host" "wait" (func $wait (param i32) (result i32)))
 (import "host" "inc" (func $inc (param i32) (result i32)))
 (table 1 funcref) (elem (i32.const 0) func $wait)
 (func (export "indirect") (param i32) (result i32) local.get 0 i32.const 0 call_indirect (type $i2i))
 (func (export "twice") (param i32) (result i32) local.get 0 call $wait call $wait)
 (func (export "trap") unreachable)
 (global $visits (mut i32) (i32.const 0))
 (func $leaf (param i32) (result i32) local.get 0 call $wait i32.const 2 i32.mul)
 (func (export "run") (param i32) (result i32)
   global.get $visits i32.const 1 i32.add global.set $visits
   local.get 0 call $leaf i32.const 1 i32.add)
 (func (export "visits") (result i32) global.get $visits)
 (func (export "native") (param $n i32) (result i32) (local $sum i32)
   (loop $again
     local.get $sum local.get $n call $inc i32.add local.set $sum
     local.get $n i32.const 1 i32.sub local.tee $n br_if $again)
   local.get $sum)
 (func (export "compute") (param $n i32) (result i32) (local $sum i32)
   (loop $again local.get $sum local.get $n i32.add local.set $sum
     local.get $n i32.const 1 i32.sub local.tee $n br_if $again) local.get $sum))

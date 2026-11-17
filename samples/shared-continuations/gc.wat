;; Copyright (C) 2026 Matt Hargett. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
(module
  (type $waittype (func (param i32) (result i32)))
  (type $box (struct (field i32)))
  (type $garbage (array i32))
  (import "host" "wait" (func $wait (param i32) (result i32)))
  (table 1 funcref)
  (elem (i32.const 0) func $wait)
  ;; Allocate 2 MiB per round, exceeding both runtimes' default GC thresholds.
  (func $churn (local $n i32)
    i32.const 128 local.set $n
    (loop $again
      i32.const 4096 array.new_default $garbage drop
      local.get $n i32.const 1 i32.sub local.tee $n br_if $again))
  (func $leaf (param $seed i32) (result i32) (local $keep (ref null $box))
    local.get $seed struct.new $box local.set $keep
    local.get $seed call $wait drop
    ;; Reading a local must not remove it from the suspended frame's root map.
    local.get $keep struct.get $box 0 drop
    call $churn
    local.get $keep struct.get $box 0 i32.const 0 call_indirect (type $waittype) drop
    local.get $keep struct.get $box 0 drop
    call $churn
    local.get $keep struct.get $box 0)
  (func (export "run") (param $seed i32) (result i32)
    (local $parent (ref null $box))
    local.get $seed i32.const 1 i32.add struct.new $box local.set $parent
    local.get $seed call $leaf
    local.get $parent struct.get $box 0 i32.add)
  (func (export "fail") (param i32) (result i32)
    local.get 0 call $wait drop unreachable)
  (func $deep (export "deep") (param $n i32) (result i32)
    local.get $n i32.eqz
    if (result i32)
      i32.const 0 call $wait
    else
      local.get $n i32.const 1 i32.sub call $deep i32.const 1 i32.add
    end))

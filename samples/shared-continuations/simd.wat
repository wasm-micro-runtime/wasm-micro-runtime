;; Copyright (C) 2026 Matt Hargett. All rights reserved.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
(module
  (type $waittype (func (param i32) (result i32)))
  (import "host" "wait" (func $wait (param i32) (result i32)))
  (table 1 funcref)
  (elem (i32.const 0) func $wait)
  (func $leaf (param $seed i32) (result i32) (local $keep v128)
    local.get $seed i32x4.splat local.set $keep
    local.get $seed call $wait drop
    local.get $keep v128.const i32x4 1 1 1 1 i32x4.add local.set $keep
    local.get $seed i32.const 0 call_indirect (type $waittype) drop
    local.get $keep i32x4.extract_lane 3)
  (func (export "run") (param $seed i32) (result i32) (local $parent v128)
    local.get $seed i32x4.splat local.set $parent
    local.get $seed call $leaf
    local.get $parent i32x4.extract_lane 2 i32.add)
  (func (export "fail") (param i32) (result i32)
    local.get 0 call $wait drop unreachable)
  (func $deep (export "deep") (param $n i32) (result i32)
    local.get $n i32.eqz
    if (result i32)
      i32.const 0 call $wait
    else
      local.get $n i32.const 1 i32.sub call $deep i32.const 1 i32.add
    end))

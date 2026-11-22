;; v128 arguments through tail calls
(module
  (type $t (func (param i32 v128 i64 v128) (result i64)))
  (table 1 funcref)
  (elem (i32.const 0) $b)
  (func $a (type $t)
    (if (result i64) (i32.eqz (local.get 0))
      (then
        (i64.add (local.get 2)
          (i64.add (i64x2.extract_lane 1 (local.get 1))
                   (i64x2.extract_lane 0 (local.get 3)))))
      (else
        (return_call $b
          (i32.sub (local.get 0) (i32.const 1))
          (local.get 3)
          (i64.add (local.get 2) (i64.const 1))
          (i64x2.add (local.get 1) (v128.const i64x2 1 2))))))
  (func $b (type $t)
    (if (result i64) (i32.eqz (local.get 0))
      (then
        (i64.sub (local.get 2)
          (i64.add (i64x2.extract_lane 0 (local.get 1))
                   (i64x2.extract_lane 1 (local.get 3)))))
      (else
        (return_call_indirect (type $t)
          (i32.sub (local.get 0) (i32.const 1))
          (local.get 3)
          (i64.add (local.get 2) (i64.const 3))
          (local.get 1)
          (i32.const 0)))))
  (func $mixed (param v128 i32) (result i32)
    (i32.add (i32x4.extract_lane 3 (local.get 0)) (local.get 1)))
  (func $to_mixed (param i32) (result i32)
    (return_call $mixed (v128.const i32x4 1 2 3 1000) (local.get 0)))
  (func (export "mixed") (param i32) (result i32)
    (call $to_mixed (local.get 0)))
  (func (export "swap") (param i32) (result i64)
    (call $a (local.get 0)
      (v128.const i64x2 10 20) (i64.const 0) (v128.const i64x2 300 400))))

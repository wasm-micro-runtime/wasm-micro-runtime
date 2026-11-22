;; A library module for tail_call_multi_module.wat
(module
  ;; constants give this function a constant pool in the fast interpreter
  (func (export "mix") (param i64 i32 f64) (result i64)
    (i64.add
      (i64.mul (local.get 0) (i64.const 1000003))
      (i64.add (i64.extend_i32_u (local.get 1))
        (i64.add (i64.trunc_f64_s (local.get 2))
          (i64.add (i64.const 1234567890123) (i64.const 987654321987))))))
  (func (export "inc") (param i32) (result i32) (i32.add (local.get 0) (i32.const 1))))

(module
  (memory 1)
  (func (export "main") (result i32)
    (i32.const 42))
  (func (export "__main_argc_argv") (param i32 i32) (result i32)
    (i32.store (i32.const 0) (local.get 0))
    (local.get 0)))

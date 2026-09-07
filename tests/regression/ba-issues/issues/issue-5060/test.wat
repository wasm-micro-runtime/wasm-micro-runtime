(module
  (table $t_extern 1 externref)
  (elem (table $t_extern) (i32.const 0) externref (ref.null extern))

  (table $t_func 1 funcref)
  (elem (table $t_func) (i32.const 0) funcref (ref.null func))

  (func (export "is_null_for_both") (result i32 i32)
    (ref.is_null (table.get $t_extern (i32.const 0)))
    (ref.is_null (table.get $t_func (i32.const 0)))
  )
)
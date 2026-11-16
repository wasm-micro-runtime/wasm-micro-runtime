(module
  (type $t (func (result i32)))
  (table (export "tbl") 4 8 funcref)
  (func $f (result i32)
    i32.const 42)
  ;; only slot 0 is initialized, slots 1..3 stay uninitialized
  (elem (i32.const 0) func $f)

  (func (export "call_indirect_i32") (param i32) (result i32)
    local.get 0
    call_indirect (type $t))

  (func (export "is_null_at") (param i32) (result i32)
    local.get 0
    table.get 0
    ref.is_null)

  (func (export "table_set_null_then_call") (param i32) (result i32)
    local.get 0
    ref.null func
    table.set 0
    local.get 0
    call_indirect (type $t))

  (func (export "table_grow_null_then_call") (param i32) (result i32)
    ref.null func
    i32.const 2
    table.grow 0
    drop
    local.get 0
    call_indirect (type $t))
)

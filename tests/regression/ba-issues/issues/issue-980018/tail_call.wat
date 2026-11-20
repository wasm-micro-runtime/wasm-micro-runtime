;; Tail calls: return_call, return_call_indirect and tail calls to imports.
(module
  (type $ii_i (func (param i32 i32) (result i32)))
  (type $acc (func (param i32 i64) (result i64)))
  (type $multi (func (param i32 i64) (result i64 i32 f64)))
  (import "env" "toupper" (func $toupper (param i32) (result i32)))

  (table 4 funcref)
  (elem (i32.const 0) $even $odd $multi_b)

  ;; a function that tail-calls itself, with its arguments permuted:
  ;; self(n, a, b) = self(n - 1, b, a + n)
  (func $self (param $n i32) (param $a i64) (param $b i64) (result i64)
    (if (result i64) (i32.eqz (local.get $n))
      (then (i64.sub (local.get $a) (local.get $b)))
      (else
        (return_call $self
          (i32.sub (local.get $n) (i32.const 1))
          (local.get $b)
          (i64.add (local.get $a) (i64.extend_i32_u (local.get $n)))))))
  (func (export "self") (param i32) (result i64)
    (call $self (local.get 0) (i64.const 0) (i64.const 0)))

  ;; its locals must be zero again after each tail call to itself
  (func $locals (param $n i32) (param $bad i32) (result i32)
    (local $l i32) (local $r funcref) (local $w i64)
    (if (i32.or (local.get $l) (i32.wrap_i64 (local.get $w)))
      (then (local.set $bad (i32.const 1))))
    (if (i32.eqz (ref.is_null (local.get $r)))
      (then (local.set $bad (i32.const 1))))
    (local.set $l (i32.const 99))
    (local.set $w (i64.const 77))
    (local.set $r (ref.func $even))
    (if (result i32) (i32.eqz (local.get $n))
      (then (local.get $bad))
      (else
        (return_call $locals
          (i32.sub (local.get $n) (i32.const 1)) (local.get $bad)))))
  (func (export "locals") (param i32) (result i32)
    (call $locals (local.get 0) (i32.const 0)))

  ;; mutual recursion through return_call and return_call_indirect
  (func $even (type $acc)
    (if (result i64) (i32.eqz (local.get 0))
      (then (local.get 1))
      (else
        (return_call $odd
          (i32.sub (local.get 0) (i32.const 1))
          (i64.add (local.get 1) (i64.const 1))))))
  (func $odd (type $acc)
    (if (result i64) (i32.eqz (local.get 0))
      (then (local.get 1))
      (else
        (return_call_indirect (type $acc)
          (i32.sub (local.get 0) (i32.const 1))
          (i64.add (local.get 1) (i64.const 3))
          (i32.const 0)))))
  (func (export "mutual") (param i32) (result i64)
    (call $even (local.get 0) (i64.const 0)))

  ;; callees of other types, whose frames differ in size from the caller's:
  ;; narrow has no locals, wide has many and more parameters
  (func $narrow (param $n i32) (param $acc i64) (result i64)
    (if (result i64) (i32.eqz (local.get $n))
      (then (local.get $acc))
      (else
        (return_call $wide
          (i32.sub (local.get $n) (i32.const 1))
          (i64.const 1) (f64.const 2) (i64.const 3) (f32.const 4)
          (i64.const 5) (i64.const 6) (i64.const 7) (i64.const 8)
          (i64.const 9) (local.get $acc)))))
  (func $wide (param $n i32)
    (param i64 f64 i64 f32 i64 i64 i64 i64 i64) (param $acc i64) (result i64)
    (local i64 i64 i64 i64 i64 i64 i64 i64)
    (local.set 11 (i64.add (local.get 1) (local.get 9)))
    (if (result i64) (i32.eqz (local.get $n))
      (then (local.get $acc))
      (else
        (return_call $narrow
          (i32.sub (local.get $n) (i32.const 1))
          (i64.add (local.get $acc) (local.get 11))))))
  (func (export "resize") (param i32) (result i64)
    (call $narrow (local.get 0) (i64.const 0)))

  ;; several results
  (func $multi_a (type $multi)
    (if (result i64 i32 f64) (i32.eqz (local.get 0))
      (then (local.get 1) (i32.const 7) (f64.const 2.5))
      (else
        (return_call $multi_b
          (i32.sub (local.get 0) (i32.const 1))
          (i64.add (local.get 1) (i64.const 1))))))
  (func $multi_b (type $multi)
    (if (result i64 i32 f64) (i32.eqz (local.get 0))
      (then (local.get 1) (i32.const 9) (f64.const 4.5))
      (else
        (return_call_indirect (type $multi)
          (i32.sub (local.get 0) (i32.const 1))
          (i64.add (local.get 1) (i64.const 3))
          (i32.const 2)))))
  (func $multi_c (type $multi) (return_call $multi_a (local.get 0) (local.get 1)))
  (func (export "multi") (param i32) (result i64 i32 f64)
    (call $multi_c (local.get 0) (i64.const 0)))

  ;; a tail call to an import from the function the host called, and from a
  ;; function below it
  (func (export "import_top") (param i32) (result i32)
    (return_call $toupper (local.get 0)))
  (func $import_mid (param i32) (result i32)
    (return_call $toupper (local.get 0)))
  (func (export "import_nested") (param i32) (result i32)
    (i32.add (call $import_mid (local.get 0)) (i32.const 1000)))

  ;; traps reached through a tail call
  (func $trap (param i32 i32) (result i32) (unreachable))
  (func $to_trap (param i32) (result i32)
    (return_call $trap (local.get 0) (local.get 0)))
  (func (export "trap") (param i32) (result i32)
    (i32.add (call $to_trap (local.get 0)) (i32.const 1)))
  (func (export "null") (param i32) (result i64)
    (return_call_indirect (type $acc) (i32.const 1) (i64.const 2) (i32.const 3)))
  (func (export "mismatch") (param i32) (result i32)
    (return_call_indirect (type $ii_i) (i32.const 1) (i32.const 2) (i32.const 0)))
  (elem declare func $even))

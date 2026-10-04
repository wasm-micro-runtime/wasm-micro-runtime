(module
  (memory 1)
  (func $copy (param $src i32) (param $dst i32) (param $len i32)
    (local $i i32)
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $len)))
        (i32.store8
          (i32.add (local.get $dst) (local.get $i))
          (i32.load8_u (i32.add (local.get $src) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop))))
  (func (export "__main_argc_argv") (param $argc i32) (param $argv i32) (result i32)
    ;; The marshalled char **argv only exists during the call:
    ;; wasm_application_execute_main() frees it before returning, so copy what
    ;; the host checks into a stable area of the linear memory.
    ;;   0: argc
    ;;   8, 12, 16: the first three char * entries of the marshalled array
    ;;   20: argv - argv[0], the size of the string area
    ;;   64, 80, 88: the first three strings
    (i32.store (i32.const 0) (local.get $argc))
    (if (i32.gt_u (local.get $argc) (i32.const 0))
      (then
        (i32.store (i32.const 8)
                   (i32.load (local.get $argv)))
        (i32.store (i32.const 20)
                   (i32.sub (local.get $argv)
                            (i32.load (local.get $argv))))
        (call $copy (i32.load (local.get $argv)) (i32.const 64)
                    (i32.const 8))))
    (if (i32.gt_u (local.get $argc) (i32.const 1))
      (then
        (i32.store (i32.const 12)
                   (i32.load offset=4 (local.get $argv)))
        (call $copy (i32.load offset=4 (local.get $argv)) (i32.const 80)
                    (i32.const 8))))
    (if (i32.gt_u (local.get $argc) (i32.const 2))
      (then
        (i32.store (i32.const 16)
                   (i32.load offset=8 (local.get $argv)))
        (call $copy (i32.load offset=8 (local.get $argv)) (i32.const 88)
                    (i32.const 8))))
    (local.get $argc)))

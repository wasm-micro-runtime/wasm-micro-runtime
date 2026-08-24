# WAMR test suites

This folder contains test scripts and cases for wamr.

## Help
```
./test_wamr.sh --help
```

## Examples
Test spec cases with fast interpreter mode, which will create folder `workspace`, download the `spec` and `wabt` repo, and build `iwasm` automatically to test spec cases:
```
./test_wamr.sh -s spec -t fast-interp
```

Test spec cases with aot mode, and use the wabt binary release package instead of compiling wabt from the source code:
```
./test_wamr.sh -s spec -t aot -b
```

Test spec cases with all modes (classic-interp/fast-interp/aot/jit):
```
./test_wamr.sh -s spec
```

Test spec cases with aot mode and pthread enabled:
```
./test_wamr.sh -s spec -t aot -p
```

Test spec cases with aot mode and SIMD enabled:
```
./test_wamr.sh -s spec -t aot -S
```

Test spec cases with fast-interp on target x86_32:
```
./test_wamr.sh -s spec -t fast-interp -m x86_32
```

When `-s` is omitted, the default test collection includes `unit`, `spec`,
`malformed`, and `standalone`. Unit tests run first with their dedicated
runtime-mode configurations:
```
./test_wamr.sh
```

Multiple test suites can also be selected explicitly by listing them after
`-s`; unit tests run before the other selected suites:
```
./test_wamr.sh -s spec unit
```

## Unit tests

Run unit tests with the selected runtime mode:
```
./test_wamr.sh -s unit -t classic-interp
./test_wamr.sh -s unit -t fast-interp
./test_wamr.sh -s unit -t jit
./test_wamr.sh -s unit -t aot
```

The `jit` option maps to the `llvm-jit` unit-test mode. Each mode is configured
and built in a separate `workspace/unittest-build-<mode>` directory, and the
results are appended to the unit test report. If `-t` is omitted, the script
processes all configured modes. `fast-jit` and `multi-tier-jit` are skipped
successfully because no unit test cases currently support them.

Use `-U` to include the full `llm-enhanced-test` suite and `-C` to collect code
coverage:
```
./test_wamr.sh -s unit -t classic-interp -U
./test_wamr.sh -s unit -t aot -C
```

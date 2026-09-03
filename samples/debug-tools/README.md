# "debug-tools" sample introduction

Tool to symoblicate stack traces. When using wasm in production, debug info are usually stripped using tools like `wasm-opt`, to decrease the binary size. If a corresponding unstripped wasm file is kept, location information (function, file, line, column) can be retrieved from the stripped stack trace.

## Build and run the sample

### Generate the stack trace

Build `iwasm` with `WAMR_BUILD_DUMP_CALL_STACK=1` and `WAMR_BUILD_FAST_INTERP=0` and the wasm file with debug info (e.g. `clang -g`). As it is done in [CMakeLists.txt](./CMakeLists.txt) and [wasm-apps/CMakeLists.txt](./wasm-apps/CMakeLists.txt) (look for `addr2line`):

```bash
$ mkdir build && cd build
$ cmake ..
$ make
$ ./iwasm wasm-apps/trap.wasm
```

The output should be something like

```text
#00: 0x0159 - $f5
#01: 0x01b2 - $f6
#02: 0x0200 - $f7
#03: 0x026b - $f8
#04: 0x236b - $f15
#05: 0x011f - _start

Exception: unreachable
```

Copy the stack trace printed to stdout into a separate file (`call_stack.txt`):

```bash
$ ./iwasm wasm-apps/trap.wasm | grep "#" > call_stack.txt
```

Same for AOT. The AOT binary has to be generated using the `--enable-dump-call-stack` option of `wamrc`, as in [CMakeLists.txt](./wasm-apps/CMakeLists.txt). Then run:

```bash
$ ./iwasm wasm-apps/trap.aot | grep "#" > call_stack.txt
```

### Symbolicate the stack trace

Run the [addr2line](../../test-tools/addr2line/addr2line.py) script to symbolicate the stack trace:

```bash
$ python3 ../../../test-tools/addr2line/addr2line.py \
    --wasi-sdk /opt/wasi-sdk \
    --wabt /opt/wabt \
    --wasm-file wasm-apps/trap.wasm \
    call_stack.txt
```

This sample also ships `symbolicate.sh` (runs the three modes shown below in
sequence) and `verify.sh` (asserts inline expansion appears in both the
classic-interp and AOT outputs — used by CI, useful locally as a smoke test).
Both expect to be run from the sample root after building:

```bash
$ ./symbolicate.sh
$ ./verify.sh
```

The output should be something like:

```text
0: trap_helper (inlined into c)
	at /path/to/wasm-micro-runtime/samples/debug-tools/wasm-apps/trap.c:9:5
   c
	at /path/to/wasm-micro-runtime/samples/debug-tools/wasm-apps/trap.c:16:12
1: b
	at /path/to/wasm-micro-runtime/samples/debug-tools/wasm-apps/trap.c:23:12
2: a
	at /path/to/wasm-micro-runtime/samples/debug-tools/wasm-apps/trap.c:29:12
3: main
	at /path/to/wasm-micro-runtime/samples/debug-tools/wasm-apps/trap.c:36:5
4: __main_void
	at ??:0
5: _start
	at wasisdk://v25.0/build/sysroot/wasi-libc-wasm32-wasi/libc-bottom-half/crt/crt1-command.c:43:13
```

Frame `0` shows **inline expansion** in action — `trap_helper` (the actual trap site)
and `c` (its caller) appear together under index `0` because `trap_helper` was inlined
into `c` by `__attribute__((always_inline))`. The `(inlined into c)` suffix on
`trap_helper` makes the relationship explicit: the runtime saw one WASM frame for `c`,
but addr2line.py reconstructs the full source-level call chain from DWARF
`DW_TAG_inlined_subroutine` entries. Each frame in the chain except the outermost
gets the `(inlined into <next>)` annotation.

### Inline expansion

addr2line.py automatically expands inline call chains when the binary's DWARF contains
`DW_TAG_inlined_subroutine` entries. This happens when functions are inlined either by
optimization (e.g. `-O2`, `-Oz`) or by `__attribute__((always_inline))`.

The included `trap.c` marks `trap_helper` as always_inline and places `__builtin_trap()`
inside it. The trap address therefore falls within the inlined region, producing the
multi-line frame `0` shown above. Each inlined frame's source location is reported
independently, so you can see exactly which inlined call chain led to the trap.

Inline expansion requires no flags — it's automatic whenever the DWARF carries the
relevant inline metadata.

For details on the tools `addr2line.py` orchestrates internally and why each
is needed, see [`test-tools/addr2line/README.md`](../../test-tools/addr2line/README.md).
The longer-form discussion of the interval-table overlay — applied to correct
the LLVM symbolizer's outermost function-name lookup on wasm — also lives there.

### Execution modes — `--mode={interp,aot,fast-interp}`

Different WAMR execution modes report call-stack offsets in different address spaces.
Pass the right `--mode` to addr2line.py so offsets are interpreted correctly:

| Mode | When | Adjustment | Source resolution |
|------|------|-----------|-------------------|
| `interp` (default) | Classic interpreter (`WAMR_BUILD_FAST_INTERP=0`) | `offset - code_start - 1` (post-advance) | Full file:line, inline expansion |
| `aot` | AOT-compiled .aot files | `offset - code_start` (wamrc commits at instruction start) | Full file:line, inline expansion |
| `fast-interp` | Fast interpreter (`WAMR_BUILD_FAST_INTERP=1`) | (not mappable) | Function-name only |

For AOT call stacks:

```bash
$ python3 ../../../test-tools/addr2line/addr2line.py \
    --wasi-sdk /opt/wasi-sdk \
    --wabt /opt/wabt \
    --wasm-file wasm-apps/trap.wasm \
    --mode aot \
    call_stack_aot.txt
```

For fast-interp call stacks:

```bash
$ python3 ../../../test-tools/addr2line/addr2line.py \
    --wasi-sdk /opt/wasi-sdk \
    --wabt /opt/wabt \
    --wasm-file wasm-apps/trap.wasm \
    --mode fast-interp \
    call_stack.txt
```

Fast-interp transforms the WASM bytecode in-memory at load time (replaces opcodes
with handler indices, expands LEBs to fixed widths). The runtime ip therefore
points into the *transformed* buffer — there's no way to map an offset back to a
source line. `--mode=fast-interp` falls back to function-name lookup (equivalent
to `--no-addr`).

If WAMR <= `1.3.2` is used, the stack trace does not contain addresses at all.
In that case, run the script with `--no-addr`: the line info returned refers to
the start of the function.

```bash
$ python3 ../../../test-tools/addr2line/addr2line.py \
    --wasi-sdk /opt/wasi-sdk \
    --wabt /opt/wabt \
    --wasm-file wasm-apps/trap.wasm \
    call_stack.txt --no-addr
```

#### sourcemap

This script also supports _sourcemap_ which is produced by [_emscripten_](https://emscripten.org/docs/tools_reference/emcc.html). The _sourcemap_ is used to map the wasm function to the original source file. To use it, add `-gsource-map` option to _emcc_ command line. The output should be a section named "sourceMappingURL" and a separated file named "_.map_.

If the wasm file is with _sourcemap_, the script will use it to get the source file and line info. It needs an extra command line option `--emsdk` to specify the path of _emsdk_. The script will use _emsymbolizer_ to query the source file and line info.

````bash
$ python3 ../../../test-tools/addr2line/addr2line.py \
    --wasi-sdk /opt/wasi-sdk \
    --wabt /opt/wabt \
    --wasm-file emscripten/wasm-apps/trap.wasm \
    --emsdk /opt/emsdk \
    call_stack.from_wasm_w_sourcemap.txt

The output should be something like:

```text
1: c
        at ../../../../../wasm-apps/trap.c:5:1
2: b
        at ../../../../../wasm-apps/trap.c:11:12
3: a
        at ../../../../../wasm-apps/trap.c:17:12
4: main
        at ../../../../../wasm-apps/trap.c:24:5
5: __main_void
        at ../../../../../../../../../emsdk/emscripten/system/lib/standalone/__main_void.c:53:10
6: _start
        at ../../../../../../../../../emsdk/emscripten/system/lib/libc/crt1.c:27:3
````

> The script assume the separated map file _.map_ is in the same directory as the wasm file.

### Another approach

If the wasm file is with "name" section, it is able to output function name in the stack trace. To achieve that, need to enable `WAMR_BUILD_LOAD_CUSTOM_SECTION` and `WAMR_BUILD_CUSTOM_NAME_SECTION`. If using .aot file, need to add `--emit-custom-sections=name` into wamrc command line options.

Then the output should be something like

```text
#00: 0x0159 - c
#01: 0x01b2 - b
#02: 0x0200 - a
#03: 0x026b - main
#04: 0x236b - __main_void
#05: 0x011f - _start

Exception: unreachable
```

Also, it is able to use _addr2line.py_ to add file and line info to the stack trace.

# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Build shared fixtures; sanitizer builds are separate from LTO measurements."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--runtime', choices=['tinywasm', 'wamr'], required=True)
p.add_argument('--root', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--feature', choices=['on', 'off'], default='on')
p.add_argument('--diagnostics', action='store_true', help='enable optional bounded guest source snapshots')
p.add_argument('--call-stacks', action='store_true', help='enable WAMR guest trap trace capture')
p.add_argument('--gc', action='store_true', help='exercise guest GC across pending calls')
p.add_argument('--simd', action='store_true', help='exercise guest SIMD across pending calls')
p.add_argument('--aot', action='store_true', help='build the WAMR AOT backend alongside the interpreter')
p.add_argument('--aot-compiler', type=Path, help='enable WAMR AOT coexistence and test compiled feature guests')
p.add_argument('--tail-calls', action='store_true', help='exercise TinyWasm nightly tail ABI and verify it was enabled')
p.add_argument('--sanitize', action='store_true', help='instrument the runtime and C clients with ASan')
p.add_argument('--client-asan', action='store_true', help='instrument C clients only; Rust remains uninstrumented')
p.add_argument('--ubsan', action='store_true', help='also enable UBSan for a Unix WAMR build')
a = p.parse_args()
win = os.name == 'nt'
mac = platform.system() == 'Darwin'
san = a.sanitize or a.client_asan
if a.diagnostics and a.feature != 'on':
    p.error('--diagnostics requires --feature on')
if a.sanitize and a.client_asan:
    p.error('choose full-runtime or client-only instrumentation')
if a.tail_calls and a.runtime != 'tinywasm':
    p.error('--tail-calls applies only to TinyWasm')
if a.client_asan and a.runtime != 'tinywasm':
    p.error('use --sanitize for the WAMR C runtime')
if a.sanitize and a.runtime == 'tinywasm' and win:
    p.error('Rust ASan does not support Windows MSVC; --client-asan is explicitly partial coverage')
if a.ubsan and (not a.sanitize or win or a.runtime != 'wamr'):
    p.error('--ubsan requires --sanitize and a Unix WAMR build')
if a.call_stacks and a.runtime != 'wamr':
    p.error('--call-stacks currently applies only to WAMR')
if a.aot and a.runtime != 'wamr':
    p.error('--aot applies only to WAMR')
if a.aot_compiler and (a.runtime != 'wamr' or not (a.gc or a.simd) or a.feature != 'on'):
    p.error('--aot-compiler requires WAMR continuations and --gc or --simd')
if a.simd and a.runtime == 'wamr' and win:
    p.error('WAMR fast-interpreter SIMD is not supported on Windows')
root = a.root.resolve()
build = a.build.resolve()
build.mkdir(parents=True, exist_ok=True)
fixture = Path(__file__).resolve().parent
env = os.environ.copy()
if win and env.get('VSINSTALLDIR'):
    cmake_tools = Path(env['VSINSTALLDIR']) / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    env['PATH'] = os.pathsep.join([str(cmake_tools/'CMake/bin'), str(cmake_tools/'Ninja'), env['PATH']])
if san:
    env['ASAN_OPTIONS'] = 'halt_on_error=1' + ('' if win else ':detect_stack_use_after_return=1')
    print('Instrumentation: ' + ('C client only; Rust runtime UNINSTRUMENTED' if a.client_asan else 'runtime and C clients; TinyWasm includes rebuilt Rust std'), flush=True)

def run(cmd):
    print(' '.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, env=env, timeout=900)

sanitize_flags = ['-fsanitize=address,undefined' if a.ubsan else '-fsanitize=address', '-fno-omit-frame-pointer']
cc = env.get('CC', 'cl' if win else 'cc')
def client(source, exe, inc, lib=None, defines=()):
    if win:
        cmd = [cc, '/nologo', '/std:c17', '/MD', '/O1' if san else '/O2', '/DLIBWASM_STATIC', '/DWASM_API_EXTERN=', '/I'+str(inc), source, '/Fe:'+str(exe), '/Fo:'+str(exe.with_suffix('.obj')), '/Fd:'+str(exe.with_suffix('.compile.pdb'))]
        cmd += ['/D'+x for x in defines]
        cmd += ['/fsanitize=address', '/Zi'] if san else ['/GL', '/Gy', '/Gw']
        if lib:
            cmd += [lib]
        cmd += ['/link', '/INCREMENTAL:NO', '/OPT:REF', '/OPT:ICF']
        if not san:
            cmd += ['/LTCG', '/Brepro']
        cmd += ['ws2_32.lib', 'userenv.lib', 'bcrypt.lib', 'ntdll.lib', 'advapi32.lib', 'kernel32.lib']
    else:
        obj = exe.with_suffix('.o')
        cmd = [cc, '-std=c11', '-O1' if san else '-O3', '-g' if san else '-flto', '-Wall', '-Wextra', '-Werror', '-fvisibility=hidden', '-I'+str(inc), '-c', source, '-o', obj]
        cmd += ['-D'+x for x in defines]
        if san:
            cmd += sanitize_flags
        run(cmd)
        cmd = [cc, '-O1' if san else '-O3', '-g' if san else '-flto', obj]
        if lib:
            cmd += [lib]
        cmd += ['-o', exe]
        if san:
            cmd += sanitize_flags
        cmd += ['-Wl,-dead_strip', '-framework', 'Security', '-framework', 'CoreFoundation', '-liconv', '-lSystem'] if mac else ['-Wl,--gc-sections', '-lm', '-lpthread', '-ldl']
    run(cmd)

if san:
    # An expected failure verifies that the C instrumentation/runtime is active.
    canary = build/'asan_canary.c'
    canary.write_text('#include <stdlib.h>\nint main(int argc,char **argv) { (void)argv; volatile char *p=malloc(8); p[argc+8]=1; free((void *)p); return 0; }\n')
    probe = build/('asan_canary.exe' if win else 'asan_canary')
    client(canary, probe, build)
    result = subprocess.run([str(probe)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (build/'asan_canary.log').write_text(result.stdout)
    if result.returncode == 0 or 'AddressSanitizer' not in result.stdout or 'heap-buffer-overflow' not in result.stdout:
        raise RuntimeError('ASan canary did not detect the expected fault; see asan_canary.log')
    print('ASan canary detected the expected heap-buffer-overflow', flush=True)

if a.runtime == 'tinywasm':
    env.update(CARGO_PROFILE_RELEASE_LTO='off' if san else 'fat', CARGO_PROFILE_RELEASE_CODEGEN_UNITS='1', CARGO_PROFILE_RELEASE_PANIC='abort')
    target = None
    cmd = ['cargo', 'rustc' if a.sanitize else 'build', '--locked', '--release', '--manifest-path', root/'Cargo.toml', '--target-dir', build/'runtime', '-p', 'tinywasm-c-api']
    features = ['external-host-calls'] if a.feature == 'on' else []
    if a.diagnostics:
        features += ['diagnostics']
    if a.tail_calls:
        features += ['tinywasm/nightly-tail-calls']
    if features:
        cmd += ['--features', ','.join(features)]
    if a.sanitize:
        info = subprocess.check_output(['rustc', '-vV'], env=env, text=True)
        target = next(line.split(': ', 1)[1] for line in info.splitlines() if line.startswith('host: '))
        env['RUSTFLAGS'] = '-Zsanitizer=address -Zexternal-clangrt -Cforce-frame-pointers=yes'
        env['CARGO_PROFILE_RELEASE_DEBUG'] = '1'
        cmd += ['-Zbuild-std', '--target', target, '--crate-type', 'staticlib']
        print('Rust instrumentation: '+env['RUSTFLAGS']+'; target='+target+'; -Zbuild-std', flush=True)
    run(cmd)
    if a.tail_calls:
        outputs = [path for name in ('output', 'stdout') for path in (build/'runtime').rglob(name)
                   if any(part == 'tinywasm' or part.startswith('tinywasm-') for part in path.parts)]
        flags = '\n'.join(path.read_text() for path in outputs)
        expected = 'cargo::rustc-cfg=tw_tail_abi="sysv64-unwind"' if win else 'cargo::rustc-cfg=tw_tail_calls'
        if expected not in flags.splitlines():
            raise RuntimeError('requested nightly tail ABI was not enabled: '+expected)
        print('Verified '+expected, flush=True)
    inc = root/'crates/c-api/include'
    lib = build/'runtime'
    if target:
        lib /= target
    lib /= 'release/tinywasm.lib' if win else 'release/libtinywasm.a'
else:
    target = 'windows' if win else 'darwin' if mac else 'linux'
    cmd = ['cmake', '-S', root/'product-mini/platforms'/target, '-B', build/'runtime', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION='+('OFF' if san else 'ON'), '-DWAMR_BUILD_INTERP=1', '-DWAMR_BUILD_FAST_INTERP=1', '-DWAMR_BUILD_AOT='+('1' if a.aot or a.aot_compiler else '0'), '-DWAMR_BUILD_JIT=0', '-DWAMR_BUILD_FAST_JIT=0', '-DWAMR_BUILD_LIBC_WASI=0', '-DWAMR_BUILD_LIBC_BUILTIN=0', '-DWAMR_BUILD_SIMD='+('1' if a.simd else '0'), '-DWAMR_BUILD_GC='+('1' if a.gc else '0'), '-DWAMR_BUILD_STRINGREF=0', '-DWAMR_BUILD_DUMP_CALL_STACK='+('1' if a.call_stacks else '0'), '-DWAMR_DISABLE_HW_BOUND_CHECK=1', '-DWAMR_BUILD_C_API_CONTINUATIONS='+('1' if a.feature=='on' else '0')]
    if a.diagnostics:
        cmd += ['-DWAMR_BUILD_C_API_DIAGNOSTICS=1']
    if san:
        flags = '/MD /O1 /Zi /fsanitize=address /DNDEBUG' if win else '-O1 -g -DNDEBUG '+' '.join(sanitize_flags)
        cmd += ['-DCMAKE_C_FLAGS_RELEASE='+flags, '-DCMAKE_CXX_FLAGS_RELEASE='+flags]
        if not win:
            cmd += ['-DCMAKE_EXE_LINKER_FLAGS='+' '.join(sanitize_flags)]
    run(cmd)
    run(['cmake', '--build', build/'runtime', '--config', 'Release', '-j', '4'])
    inc = build/'include'
    inc.mkdir(exist_ok=True)
    shutil.copy2(root/'core/iwasm/include/wasm_c_api.h', inc/'wasm.h')
    if a.feature == 'on':
        shutil.copy2(root/'core/iwasm/include/wasm_ext.h', inc/'wasm_ext.h')
    if a.diagnostics:
        shutil.copy2(root/'core/iwasm/include/wasm_ext_debug.h', inc/'wasm_ext_debug.h')
        shutil.copy2(root/'core/iwasm/include/wasm_c_api.h', inc/'wasm_c_api.h')
    libraries = list((build/'runtime').rglob('libiwasm.lib' if win else 'libiwasm.a'))
    assert len(libraries) == 1, libraries
    lib = libraries[0]
for name in ['fixture'] + (['edges'] if a.feature == 'on' else []):
    exe = build/(name+('.exe' if win else ''))
    client(fixture/(name+'.c'), exe, inc, lib, ['TEST_CONTINUATIONS'] if a.feature=='on' else [])
    run([exe, fixture/'fixture.wasm'] if name=='fixture' else [exe])

if a.runtime == 'tinywasm':
    exe = build/('api.exe' if win else 'api')
    client(root/'crates/c-api/tests/api.c', exe, inc, lib)
    run([exe, root/'crates/c-api/tests/fixtures'])

if a.feature == 'on' and (a.gc or a.simd):
    exe = build/('features.exe' if win else 'features')
    client(fixture/'features.c', exe, inc, lib, ['TEST_WAMR'] if a.runtime == 'wamr' else [])
    for name, enabled in [('gc', a.gc), ('simd', a.simd)]:
        if not enabled:
            continue
        run([exe, fixture/(name+'.wasm')])
        if a.aot_compiler:
            output = build/(name+'.aot')
            run([a.aot_compiler.resolve(), '--bounds-checks=1', '--stack-bounds-checks=1',
                 *(['--enable-gc'] if a.gc else []),
                 '-o', output, fixture/(name+'.wasm')])
            run([exe, output, 'aot'])

if a.diagnostics:
    exe = build/('diagnostics.exe' if win else 'diagnostics')
    client(fixture/'debug/fixture.c', exe, inc, lib)
    run([exe, fixture/'debug/source_guest.wasm'])

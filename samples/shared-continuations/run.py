# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Build the identical C fixture against either runtime. Run from a compiler developer shell on Windows."""
import argparse, os, platform, shutil, subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--runtime', choices=['tinywasm','wamr'], required=True)
p.add_argument('--root', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--feature', choices=['on','off'], default='on')
p.add_argument('--sanitize', action='store_true')
a=p.parse_args()
if a.sanitize and a.runtime == 'tinywasm':
 p.error('--sanitize instruments the WAMR C runtime; use Miri for the TinyWasm Rust adapter')
root=a.root.resolve();build=a.build.resolve();build.mkdir(parents=True,exist_ok=True)
fixture=Path(__file__).resolve().parent; win=os.name=='nt';mac=platform.system()=='Darwin';env=os.environ.copy()
def run(cmd):
 print(' '.join(map(str,cmd)),flush=True);subprocess.run(list(map(str,cmd)),check=True,env=env)
if a.runtime=='tinywasm':
 env.update(CARGO_PROFILE_RELEASE_LTO='fat',CARGO_PROFILE_RELEASE_CODEGEN_UNITS='1',CARGO_PROFILE_RELEASE_PANIC='abort')
 cmd=['cargo','build','--locked','--release','--manifest-path',root/'Cargo.toml','--target-dir',build/'runtime','-p','tinywasm-c-api']
 if a.feature=='on':cmd+=['--features','external-host-calls']
 run(cmd);inc=root/'crates/c-api/include';lib=build/'runtime/release'/('tinywasm.lib' if win else 'libtinywasm.a')
else:
 target='windows' if win else 'darwin' if mac else 'linux'
 cmd=['cmake','-S',root/'product-mini/platforms'/target,'-B',build/'runtime','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON','-DWAMR_BUILD_INTERP=1','-DWAMR_BUILD_FAST_INTERP=1','-DWAMR_BUILD_AOT=0','-DWAMR_BUILD_JIT=0','-DWAMR_BUILD_FAST_JIT=0','-DWAMR_BUILD_LIBC_WASI=0','-DWAMR_BUILD_LIBC_BUILTIN=0','-DWAMR_BUILD_SIMD=0','-DWAMR_DISABLE_HW_BOUND_CHECK=1','-DWAMR_BUILD_C_API_CONTINUATIONS='+('1' if a.feature=='on' else '0')]
 if a.sanitize:cmd+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
 run(cmd);run(['cmake','--build',build/'runtime','--config','Release','-j','4'])
 inc=build/'include';inc.mkdir(exist_ok=True);shutil.copy2(root/'core/iwasm/include/wasm_c_api.h',inc/'wasm.h');
 if a.feature=='on':shutil.copy2(root/'core/iwasm/include/wasm_ext.h',inc/'wasm_ext.h')
 candidates=list((build/'runtime').rglob('libiwasm.lib' if win else 'libiwasm.a'));assert len(candidates)==1,candidates
 lib=candidates[0]
exe=build/('fixture.exe' if win else 'fixture')
if win:
 cmd=['cl','/nologo','/std:c17','/O2','/GL','/Gy','/Gw','/DLIBWASM_STATIC','/DWASM_API_EXTERN=','/I'+str(inc),fixture/'fixture.c',lib,'/Fe:'+str(exe)]
 if a.feature=='on':cmd+=['/DTEST_CONTINUATIONS']
 cmd+=['/link','/LTCG','/OPT:REF','/OPT:ICF','/INCREMENTAL:NO','/Brepro','ws2_32.lib','userenv.lib','bcrypt.lib','ntdll.lib','advapi32.lib','kernel32.lib']
else:
 # Keep fixture warnings strict without applying -Werror to backend code at LTO link.
 obj=build/'fixture.o'
 cmd=[env.get('CC','cc'),'-std=c11','-O3','-Wall','-Wextra','-Werror','-flto','-fvisibility=hidden','-I'+str(inc),'-c',fixture/'fixture.c','-o',obj]
 if a.feature=='on':cmd+=['-DTEST_CONTINUATIONS']
 if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 run(cmd)
 cmd=[env.get('CC','cc'),'-O3','-flto','-fvisibility=hidden',obj,lib,'-o',exe]
 if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 if mac:cmd+=['-Wl,-dead_strip','-framework','Security','-framework','CoreFoundation','-liconv','-lSystem']
 else:cmd+=['-Wl,--gc-sections','-lm','-lpthread','-ldl']
run(cmd);run([exe,fixture/'fixture.wasm'])

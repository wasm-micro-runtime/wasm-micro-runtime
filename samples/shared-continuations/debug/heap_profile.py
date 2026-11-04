# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Compare live macOS allocator state in separate, instrumented fixture builds."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser()
    p.add_argument('builds', type=Path)
    p.add_argument('--fixture', type=Path, required=True, help='shared fixture.c')
    p.add_argument('--tiny-include', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--samples', type=int, default=5)
    a = p.parse_args()
    if platform.system() != 'Darwin' or a.samples < 1:
        p.error('requires macOS and at least one sample')
    fixture = a.fixture.resolve()
    source = fixture.read_text()
    marker = '    wasm_func_delete(run);'
    if source.count(marker) != 1:
        raise RuntimeError('fixture teardown changed; review the sampling boundary')
    # Stop before teardown, after the same short semantic workload in both
    # builds. This is live heap, not peak RSS or allocator high-water usage.
    source = '#include <malloc/malloc.h>\n' + source.replace(marker, '''    malloc_statistics_t stats;
    malloc_zone_statistics(NULL, &stats);
    printf("heap %u %zu %zu\\n", stats.blocks_in_use, stats.size_in_use, stats.size_allocated);
''' + marker)
    env = os.environ.copy()
    env['WASM_EXT_TEST_ONLY'] = '1'
    cc = env.get('CC', 'clang')
    result = {}
    with tempfile.TemporaryDirectory(dir=a.builds.resolve(), prefix='heap-') as directory:
        work = Path(directory)
        c = work/'fixture.c'
        c.write_text(source)
        # A common executable path avoids counting different dyld path-string
        # allocation sizes as a runtime difference.
        measured = work/'measure'
        for runtime in ['wamr', 'tinywasm']:
            for feature in ['on', 'off']:
                key = runtime+'_'+feature
                result[key] = {}
                for phase in ['base', 'after']:
                    build = a.builds.resolve()/(phase+'_'+key)
                    inc = build/'include' if runtime == 'wamr' else a.tiny_include.resolve()
                    lib = build/('runtime/libiwasm.a' if runtime == 'wamr' else 'runtime/release/libtinywasm.a')
                    obj, exe = work/'fixture.o', work/'linked'
                    cmd = [cc, '-std=c11', '-O3', '-flto', '-fvisibility=hidden', '-I'+str(inc), '-I'+str(fixture.parent), '-c', str(c), '-o', str(obj)]
                    if feature == 'on':
                        cmd += ['-DTEST_CONTINUATIONS']
                    subprocess.run(cmd, check=True)
                    subprocess.run([cc, '-O3', '-flto', str(obj), str(lib), '-o', str(exe), '-Wl,-dead_strip', '-framework', 'Security', '-framework', 'CoreFoundation', '-liconv'], check=True)
                    shutil.copy2(exe, measured)
                    samples = []
                    for _ in range(a.samples):
                        output = subprocess.check_output([str(measured), str(fixture.with_suffix('.wasm'))], env=env, text=True)
                        values = next(line.split()[1:] for line in output.splitlines() if line.startswith('heap '))
                        samples.append(dict(zip(['blocks_in_use', 'size_in_use', 'size_allocated'], map(int, values))))
                    result[key][phase] = samples
                print(key, result[key], flush=True)
    a.output.write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    main()

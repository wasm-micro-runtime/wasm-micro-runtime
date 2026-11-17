#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Resolve a validated, single-instance wit-component HTTP guest offline."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from component import OPS, Unsupported, children, emit, one, parse

HTTP = '"wasi:http/types@0.3.2"'
CLIENT = '"wasi:http/client@0.3.2"'


def named(node, name):
    return one([x[2] for x in children(node, ['export']) if x[1] == name],
               f'export {name}')


def resolve(component):
    if any(node[1] == CLIENT for node in children(component, ['import'])):
        raise Unsupported('wasi:http/client.send requires outbound HTTP transport')
    modules = children(component, ['core', 'module'])
    if len(modules) not in (1, 3):
        raise Unsupported('expected one guest with an optional wit-component shim')
    guest = modules[0]
    instances, core_funcs, functions, types, imports = [], [], [], [], []
    guest_instance = fixups = shim = None
    tables = []
    exports = {}
    for node in component[1:]:
        match node:
            case ['import', interface, ['instance', *_]]:
                imports.append(interface)
            case ['instance', *_] | ['export', _, ['instance', _], *_]:
                imports.append(None)
            case ['type', ty]:
                types.append(ty)
            case ['alias', 'export', index, name, ['type']]:
                types.append((imports[int(index)], name))
            case ['alias', 'export', index, name, ['func']]:
                functions.append((imports[int(index)], name))
            case ['alias', 'core', 'export', index, name, ['core', 'table']]:
                tables.append((int(index), name))
            case ['alias', 'core', 'export', index, name, ['core', 'func']]:
                core_funcs.append(('alias', int(index), name))
            case ['core', 'func', ['canon', op, *options]]:
                if op == 'lower':
                    interface, name = functions[int(options[0][1])]
                    if interface is None:
                        raise Unsupported('canonical lower must target a host import')
                    target = ('wasi-http', json.loads(name)) if interface == HTTP else ('unsupported-wasi', json.loads(interface) + '#' + json.loads(name))
                elif op == 'resource.drop':
                    interface, name = types[int(options[0])]
                    target = ('wasi-http', '[drop]' + json.loads(name)) if interface == HTTP else ('unsupported-wasi', json.loads(interface) + '#drop-' + json.loads(name))
                elif op in OPS:
                    element = 'unit'
                    if op.startswith(('future.', 'stream.')):
                        ty = types[int(options[0])]
                        if ty == ['stream', 'u8']:
                            element = 'u8'
                        elif ty[0] == 'future' and len(ty) == 2:
                            result = types[int(ty[1])]
                            error = result[-1]
                            if result[0] != 'result' or not isinstance(error, list) or error[0] != 'error' or types[int(error[1])] != (HTTP, '"error-code"'):
                                raise Unsupported('unsupported future payload ' + repr(result))
                            element = 'http-status'
                            if len(result) > 2:
                                option = types[int(result[1])]
                                if option[0] != 'option' or types[int(types[int(option[1])][1])] not in ((HTTP, '"fields"'), (HTTP, '"trailers"')):
                                    raise Unsupported('unsupported HTTP future option')
                                element = 'http-trailers'
                        else:
                            raise Unsupported('unsupported endpoint type ' + repr(ty))
                    if op == 'task.return' and options and options[0][0] == 'result':
                        result = types[int(options[0][1])]
                        target = ('wasi-http', '[return]run' if result == ['result'] else '[return]handle')
                    else:
                        target = ('p3', f'{op}:{element}:{int("async" in options)}')
                else:
                    target = ('unsupported-wasi', op)
                core_funcs.append(target)
            case ['core', 'instance', ['instantiate', index, *bindings]]:
                index = int(index)
                bound = {name: int(instance) for _, name, (_, instance) in bindings}
                if index == 0 and guest_instance is None:
                    guest_instance = len(instances)
                    instances.append(bound)
                elif index == 1 and shim is None and not bound:
                    shim = len(instances)
                    instances.append(None)
                elif index == 2 and fixups is None and set(bound) == {'"actual"', '"shim"'} and bound['"shim"'] == shim:
                    fixups = (instances[bound['"actual"']], '"actual"', '"shim"')
                    instances.append(None)
                elif index == 2 and fixups is None and set(bound) == {'""'}:
                    exports_ = instances[bound['""']].copy()
                    table = exports_.pop('"$imports"')
                    if table[0] != 'table' or tables[table[1]] != (shim, '"$imports"'):
                        raise Unsupported('fixup must initialize the shim table')
                    fixups = (exports_, '""', '""')
                    instances.append(None)
                else:
                    raise Unsupported('unexpected core instantiation')
            case ['core', 'instance', *bindings]:
                instances.append({name: (kind, int(idx)) for _, name, (kind, idx) in bindings})
            case ['func', *parts]:
                lift = one(children(parts, ['canon', 'lift']), 'canonical lift')
                alias = core_funcs[int(lift[2][2])]
                if alias[:2] != ('alias', guest_instance):
                    raise Unsupported('lift must resolve to a guest function')
                name = json.loads(alias[2])
                if name == 'wasi:cli/run@0.2.0#run':
                    exports['run'] = alias[2]
                async_exports = {'[async-lift]wasi:http/handler@0.3.2#handle': 'handle',
                                 '[async-lift]wasi:cli/run@0.3.2#run': 'command'}
                if name in async_exports:
                    entry = async_exports[name]
                    exports[entry] = alias[2]
                    callback = one(children(lift, ['callback']), 'handler callback')
                    cb = core_funcs[int(callback[1])]
                    if cb[:2] != ('alias', guest_instance):
                        raise Unsupported('handler callback must resolve to guest function')
                    exports[entry + '-event'] = cb[2]
                functions.append(None)
            case _:
                pass

    if len(modules) == 3:
        if fixups is None:
            raise Unsupported('missing shim fixup bindings')
        bindings, function_ns, table_ns = fixups
        validate_shim(modules[1], modules[2], bindings, function_ns, table_ns)
    if guest_instance is None or not exports:
        raise Unsupported('missing supported component entry point')

    output = ['module']
    for item in guest[2:]:
        if item[0] == 'import':
            _, module, name, desc = item
            if desc[0] != 'func':
                raise Unsupported('core imports must all be functions')
            kind, idx = instances[instances[guest_instance][module]][name]
            if kind != 'func':
                raise Unsupported('core function import resolved to a non-function')
            binding = core_funcs[idx]
            if binding[0] == 'alias':
                if binding[1] != shim or fixups is None:
                    raise Unsupported('import alias must point at a validated shim')
                kind, idx = fixups[0][binding[2]]
                if kind != 'func':
                    raise Unsupported('shim fixup must supply a function')
                binding = core_funcs[idx]
            if binding[0] == 'alias':
                raise Unsupported('unresolved core import')
            output.append(['import', json.dumps(binding[0]), json.dumps(binding[1]), desc])
        elif item[0] == 'start':
            output.append(['export', '"__p3_start"', ['func', item[1]]])
        elif item[0] != 'export':
            output.append(item)
    one(children(guest, ['memory']), 'guest memory')
    output.append(['export', '"__p3_memory"', ['memory', '0']])
    for name, original in exports.items():
        output.append(['export', json.dumps(name), named(guest, original)])
    alloc = [x for x in children(guest, ['export']) if x[1] in ('"cabi_realloc"', '"cabi_realloc_wit_bindgen_0_60_0"')]
    if any(x[1] == '"native-loop"' for x in children(guest, ['export'])):
        output.append(['export', '"native-loop"', named(guest, '"native-loop"')])
    if alloc:
        output.append(['export', '"cabi_realloc"', alloc[0][2]])
    return output


def validate_shim(shim, fixup, bindings, function_ns, table_ns):
    count = len(bindings)
    table = ['table', str(count), str(count), 'funcref']
    if one(children(shim, ['table']), 'shim table') != table:
        raise Unsupported('unexpected shim table')
    if any(n[0] not in {'type', 'table', 'export', 'func'} for n in shim[2:]):
        raise Unsupported('unexpected shim code')
    funcs = children(shim, ['func'])
    if len(funcs) != count or named(shim, '"$imports"') != ['table', '0']:
        raise Unsupported('unexpected shim exports')
    for i, func in enumerate(funcs):
        idx = str(i)
        use = func[1]
        signature = children(shim, ['type'])[int(use[1])][1]
        params = sum(len(n) - 1 for n in children(signature, ['param']))
        start = 2
        while isinstance(func[start], list) and func[start][0] in {'param', 'result'}:
            start += 1
        code = func[start:]
        expected = [token for p in range(params) for token in ('local.get', str(p))]
        expected += ['i32.const', idx, 'call_indirect', use]
        if code != expected or named(shim, json.dumps(idx)) != ['func', idx]:
            raise Unsupported('unexpected shim trampoline')
    imports = children(fixup, ['import'])
    if (any(n[0] not in {'type', 'import', 'elem'} for n in fixup[2:])
            or len(imports) != count + 1 or imports[-1] != ['import', table_ns, '"$imports"', table]
            or any(imp[:3] != ['import', function_ns, json.dumps(str(i))] or imp[3][0] != 'func'
                   for i, imp in enumerate(imports[:-1]))
            or one(children(fixup, ['elem']), 'shim element initializer')
            != ['elem', ['i32.const', '0'], 'func', *map(str, range(count))]):
        raise Unsupported('unexpected shim fixup module')


def link(source, output, tool):
    with tempfile.TemporaryDirectory() as tmp:
        stripped = Path(tmp) / 'numeric.wasm'
        wat = Path(tmp) / 'linked.wat'
        subprocess.run([tool, 'validate', '--features', 'all', source], check=True)
        subprocess.run([tool, 'strip', '-a', source, '-o', stripped], check=True)
        component = one(parse(subprocess.check_output([tool, 'print', stripped], text=True)), 'component')
        wat.write_text(emit(resolve(component)))
        subprocess.run([tool, 'parse', wat, '-o', output], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--wasm-tools', default='wasm-tools')
    args = parser.parse_args()
    link(args.source, args.output, args.wasm_tools)

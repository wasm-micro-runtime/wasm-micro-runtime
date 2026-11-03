#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Offline linker for the deliberately restricted single-memory numeric profile.

Resolve canonical imports from the component graph, rather than naming stub
imports in an extracted module. The memory-only provider is statically fused
with its sole consumer; memory identity and all canonical operations remain.
Unsupported graphs/types fail closed. This is not a general component loader.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


class Unsupported(ValueError):
    pass


def parse(text):
    tokens = []
    i = 0
    while i < len(text):
        if text[i].isspace():
            i += 1
        elif text.startswith(';;', i):
            i = text.find('\n', i)
            if i == -1:
                break
        elif text.startswith('(;', i):
            depth = 1
            i += 2
            while depth:
                if i == len(text):
                    raise ValueError('unterminated comment')
                if text.startswith('(;', i):
                    depth += 1
                    i += 2
                elif text.startswith(';)', i):
                    depth -= 1
                    i += 2
                else:
                    i += 1
        elif text[i] in '()':
            tokens.append(text[i])
            i += 1
        elif text[i] == '"':
            start = i
            i += 1
            while i < len(text) and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            if i == len(text):
                raise ValueError('unterminated string')
            i += 1
            tokens.append(text[start:i])
        else:
            m = re.match(r'[^\s()]+', text[i:])
            tokens.append(m[0])
            i += len(m[0])
    stack = [[]]
    for token in tokens:
        if token == '(':
            node = []
            stack[-1].append(node)
            stack.append(node)
        elif token == ')':
            if len(stack) == 1:
                raise ValueError('unexpected closing parenthesis')
            stack.pop()
        else:
            stack[-1].append(token)
    if len(stack) != 1:
        raise ValueError('unterminated expression')
    return stack[0]


def emit(node):
    return '(' + ' '.join(map(emit, node)) + ')' if isinstance(node, list) else node


def children(node, prefix):
    return [n for n in node if isinstance(n, list) and n[:len(prefix)] == prefix]


def one(nodes, description):
    if len(nodes) != 1:
        raise Unsupported('requires one ' + description)
    return nodes[0]


OPS = {'stream.new', 'stream.read', 'stream.write', 'stream.cancel-read',
       'stream.cancel-write', 'stream.drop-readable', 'stream.drop-writable',
       'future.new', 'future.read', 'future.write', 'future.cancel-read',
       'future.cancel-write', 'future.drop-readable', 'future.drop-writable',
       'waitable-set.new', 'waitable-set.poll', 'waitable-set.wait', 'waitable-set.drop', 'waitable.join',
       'task.return', 'task.cancel', 'context.get', 'context.set'}


def lower(component):
    allowed = {'type', 'canon', 'func'}
    for item in component:
        if isinstance(item, list) and item[0] not in allowed and item[:2] not in (['core', 'module'], ['core', 'instance']):
            raise Unsupported('unsupported component field: ' + item[0])
    modules = children(component, ['core', 'module'])
    providers = [m for m in modules if len(children(m, ['memory'])) == 1
                 and not children(m, ['func'])]
    provider = one(providers, 'memory provider')
    if len(modules) != 2:
        raise Unsupported('profile requires a memory provider and one core consumer')
    memory = one(children(provider, ['memory']), 'provider memory')
    if any(isinstance(n, list) and n[0] not in {'memory', 'export'} for n in provider[3:]):
        raise Unsupported('memory provider must have no code or initialization')
    if children(provider, ['export']):
        raise Unsupported('use an inline provider memory export')
    memory_export = one(children(memory, ['export']), 'provider memory export')[1]
    consumers = [m for m in modules if m is not provider]
    core = consumers[0]
    instances = children(component, ['core', 'instance'])
    if len(instances) != 2:
        raise Unsupported('profile requires exactly two core instantiations')
    provider_inst = one([n for n in instances if children(n, ['instantiate'])
                         and children(n, ['instantiate'])[0][1] == provider[2]], 'memory instance')
    inst = one([n for n in instances if children(n, ['instantiate'])
                and children(n, ['instantiate'])[0][1] == core[2]], 'consumer instance')
    bindings = one(children(one(children(inst, ['instantiate']), 'instantiate'), ['with']), 'import namespace')
    binding_instance = one(children(bindings, ['instance']), 'import instance')
    aliases = {n[1]: n[2] for n in children(binding_instance, ['export'])}
    types = {n[1]: n[2] for n in children(component, ['type'])}
    canons = {}
    for canon in children(component, ['canon']):
        if canon[1] not in OPS:
            raise Unsupported('unsupported canonical operation: ' + canon[1])
        name = one(children(canon, ['core', 'func']), 'canonical function name')[2]
        if canon[1].startswith('context.') and canon[2:4] != ['i32', '0']:
            raise Unsupported('only i32 context slot zero is supported')
        element = 'unit'
        if canon[1].startswith(('stream.', 'future.')):
            ty = types.get(canon[2])
            if not ty or ty[0] != canon[1].split('.')[0] or ty[1:] not in ([], ['u8'], ['bool'], ['char']):
                raise Unsupported('stream/future element type outside profile')
            element = ty[1] if ty[1:] else 'unit'
        for option in children(canon, ['memory']):
            if option[1] != ['core', 'memory', provider_inst[2], memory_export]:
                raise Unsupported('canonical operation uses a different memory')
        canons[name] = canon[1] + ':' + element + ':' + str(int('async' in canon))
    output = ['module']
    imported_memory = []
    for item in children(core, ['import']):
        if item[1] != bindings[1] or item[2] not in aliases:
            raise Unsupported('unresolved core import ' + emit(item))
        target = aliases[item[2]]
        desc = item[3]
        if desc[0] == 'memory':
            if target != ['memory', provider_inst[2], memory_export]:
                raise Unsupported('memory import identity mismatch')
            imported_memory.append(desc)
        elif desc[0] == 'func' and target[0] == 'func' and target[1] in canons:
            output.append(['import', '"p3"', json.dumps(canons[target[1]]), desc])
        else:
            raise Unsupported('unresolved canonical function ' + emit(item))
    imported = one(imported_memory, 'imported memory')
    limits = [n for n in memory[1:] if not isinstance(n, list) and not n.startswith('$')]
    import_limits = [n for n in imported[1:] if not n.startswith('$')]
    if import_limits != limits:
        raise Unsupported('memory limits must match for static fusion')
    output.append(['memory', ['export', '"__p3_memory"'], *limits])
    for item in core[3:]:
        if isinstance(item, list) and item[0] != 'import':
            if item[0] == 'start':
                output.append(['export', '"__p3_start"', ['func', item[1]]])
                continue
            if item[0] == 'memory':
                raise Unsupported('additional memory/table outside profile')
            output.append(item)
    exports = {}
    for fun in children(component, ['func']):
        exp = children(fun, ['export'])
        lift = children(fun, ['canon', 'lift'])
        if len(exp) != 1 or len(lift) != 1 or lift[0][2][:3] != ['core', 'func', inst[2]]:
            raise Unsupported('requires direct numeric canonical lifts')
        exports[exp[0][1]] = lift[0][2][3]
    return output, exports


def compile_script(source, out, wasm_tools):
    out.mkdir(parents=True, exist_ok=True)
    definitions, cases = {}, []
    validated = set()
    current = None
    asserted = False
    for node in parse(source.read_text()):
        if node[:2] == ['component', 'definition']:
            definitions[node[2]] = node
        elif node[:2] == ['component', 'instance']:
            current = definitions[node[3]]
            asserted = False
        elif node[0] == 'component':
            current = node
            asserted = False
        elif node[0] in {'assert_return', 'assert_trap'}:
            if current is None:
                raise Unsupported('assertion has no component instance')
            try:
                if asserted:
                    raise Unsupported('successive assertions on one live instance outside profile')
                core, exports = lower(current)
            except Unsupported as error:
                cases.append({'status': 'UNSUPPORTED', 'assertion': emit(node), 'reason': str(error)})
                asserted = True
                continue
            asserted = True
            if id(current) not in validated:
                component = ['component', *current[3:]] if current[:2] == ['component', 'definition'] else current
                original = out / f'component-{len(validated)}.wat'
                original.write_text(emit(component))
                subprocess.run([wasm_tools, 'parse', original, '-o', original.with_suffix('.wasm')], check=True)
                validated.add(id(current))
            name = 'case-' + str(len(cases))
            wat = out / (name + '.wat')
            wat.write_text(emit(core))
            binary = out / (name + '.wasm')
            subprocess.run([wasm_tools, 'parse', str(wat), '-o', str(binary)], check=True)
            invoke = node[1]
            if invoke[0] != 'invoke':
                raise Unsupported('only direct component export invocations')
            expected = ('trap:' + json.loads(node[2]) if node[0] == 'assert_trap'
                        else str(int(node[2][1], 0)) if len(node) > 2 else 'void')
            cases.append({'module': binary.name, 'export': json.loads(exports[invoke[1]]),
                          'expected': expected, 'args': [1 if x == ['bool.const', 'true'] else 0 if x == ['bool.const', 'false'] else int(x[1], 0) for x in invoke[2:]]})
        else:
            raise Unsupported('unsupported script directive ' + node[0])
    manifest = {'source': str(source), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'profile': 'single-memory-static-numeric-u8-unit', 'cases': cases}
    (out / 'cases.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--validator', required=True)
    a = p.parse_args()
    compile_script(a.source, a.output, a.validator)

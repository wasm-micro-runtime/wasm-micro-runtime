# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Load explicitly in GDB or LLDB, then use wasm-bt. No inferior expressions/calls."""
import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('wasm_symbolize', Path(__file__).with_name('symbolize.py'))
symbolize = importlib.util.module_from_spec(spec)
spec.loader.exec_module(symbolize)

def lldb_trace(debugger, command, result, internal_dict):
    import lldb
    try:
        target = debugger.GetSelectedTarget()
        pointer = target.FindFirstGlobalVariable('debug_published')
        if not pointer.IsValid():
            raise ValueError('missing consumer debug_published symbol')
        error = lldb.SBError()
        data = target.GetProcess().ReadMemory(pointer.GetValueAsUnsigned(), symbolize.RECORD, error)
        if not error.Success():
            raise ValueError(str(error))
        result.AppendMessage(symbolize.render(data))
    except Exception as error:
        result.SetError(str(error))

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f '+__name__+'.lldb_trace wasm-bt')

try:
    import gdb
except ImportError:
    pass
else:
    class WasmTrace(gdb.Command):
        def __init__(self):
            super().__init__('wasm-bt', gdb.COMMAND_STACK)
        def invoke(self, args, from_tty):
            try:
                symbol = gdb.lookup_global_symbol('debug_published')
                data = bytes(gdb.selected_inferior().read_memory(int(symbol.value()), symbolize.RECORD))
                gdb.write(symbolize.render(data))
            except Exception as error:
                raise gdb.GdbError(str(error))
    WasmTrace()

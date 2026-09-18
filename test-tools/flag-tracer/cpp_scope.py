# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Map a source line to the WASM_* macros guarding it.

Lexical #if/#else/#endif block scan -- no expression evaluation.  A condition
is reduced to a list of *alternatives*: `A && B` is one alternative that needs
both switches, `A || B` is two independent ones.  A line's guards are the
product of the alternatives of every enclosing block.

A changed line that is part of a directive is a case of its own: adding
`|| WASM_ENABLE_SHARED_HEAP != 0` to an `#if` chain is a change to that
switch, whatever else the chain mentions.
"""

import re

_DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
_MACRO = re.compile(r"\b(WASM_(?:ENABLE|DISABLE)_[A-Z0-9_]+)\b")
# `#if WASM_ENABLE_GC == 0` guards the *disabled* path -- a common idiom here,
# so the comparison is worth reading even though the expression is not evaluated
_CMP = re.compile(r"^\s*(==|!=)\s*(\d+)")

PREPROCESSOR = "preprocessor"
MACRO_EDIT = "macro-edit"

# guard chains here run to a dozen alternatives; keep the product across
# nested blocks bounded
_MAX_ALTS = 16


def _occurrences(expr):
    """[(macro, on)] for one term, honouring `== 0` and a leading `!`."""
    out = []
    for m in _MACRO.finditer(expr):
        on = True
        cmp_ = _CMP.match(expr[m.end():])
        if cmp_:
            zero = cmp_.group(2) == "0"
            on = zero != (cmp_.group(1) == "==")
        elif expr[:m.start()].rstrip().endswith("!"):
            on = False
        out.append((m.group(1), on))
    return out


def _alternatives(expr):
    """Condition -> [alternative]; alternative = [(macro, on)], a conjunction.

    Split on `||`, each side treated as a conjunction of the macros it names.
    Parenthesisation is not modelled -- being a little wide here is the trade.
    """
    alts = [_occurrences(part) for part in expr.split("||")]
    return [a for a in alts if a] or [[]]


def _negate(alts):
    """not (A or B) == (not A) and (not B), distributed back to alternatives."""
    out = [[]]
    for alt in alts:
        grown = [prefix + [(macro, not on)]
                 for macro, on in alt          # not (x and y) == !x or !y
                 for prefix in out]
        out = grown[:_MAX_ALTS] or out
    return out


def _product(frames):
    """Combine the alternatives of every enclosing block."""
    out = [()]
    for alts in frames:
        grown = [prefix + tuple(alt) for alt in alts for prefix in out]
        out = grown[:_MAX_ALTS] or out
    return tuple(out)


def _current(stack):
    return _product([_negate(alts) if neg else alts for alts, neg in stack])


def scan(text):
    """Return {line: (alternatives, source)} for every line.

    `alternatives` is a tuple of guard tuples ((macro, on), ...); the code is
    compiled when *any one* of them holds.  Within a guard tuple the entries
    are ordered outermost first, so a caller can let the innermost win when the
    same macro appears twice (`#if A || B` wrapping `#if A == 0`).
    """
    out = {}
    stack = []      # [alternatives, negated]
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = _DIRECTIVE.match(lines[i])
        if not m:
            out[i + 1] = (_current(stack), PREPROCESSOR)
            i += 1
            continue

        kind, rest = m.group(1), m.group(2)
        physical = [i]
        while rest.rstrip().endswith("\\") and i + 1 < len(lines):
            i += 1
            physical.append(i)
            rest = rest.rstrip()[:-1] + " " + lines[i]

        if kind in ("if", "ifdef", "ifndef"):
            stack.append([_alternatives(rest), kind == "ifndef"])
        elif kind == "elif":
            if stack:
                stack[-1] = [_alternatives(rest), False]
        elif kind == "else":
            if stack:
                stack[-1][1] = not stack[-1][1]
        elif kind == "endif":
            if stack:
                stack.pop()

        for no in physical:
            # `|| A || B` on one edited line is two alternatives, not a pair
            named = (_alternatives(lines[no])
                     if kind != "endif" and _MACRO.search(lines[no]) else [])
            out[no + 1] = ((tuple(tuple(a) for a in named), MACRO_EDIT)
                           if named else (_current(stack), PREPROCESSOR))
        i += 1
    return out


def macros_at(text, lines):
    """Return {line: (alternatives, source)} restricted to `lines`."""
    table = scan(text)
    return {n: table[n] for n in lines if n in table}

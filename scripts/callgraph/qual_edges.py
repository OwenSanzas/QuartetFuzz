#!/usr/bin/env python3
"""Qualified-call edges for C++: `Cls<A, ..>::fn(..)` / `Cls::template fn<..>(..)`
links to the definitions of fn inside class Cls, without merge_final's CG_CAP.

glaze dispatches everything as to<BEVE, T>::template op<Opts>(..): 629
functions are named op, so the plain name edge (<= CG_CAP definitions) is
dropped.  Here the qualifier picks them: the op of every `struct to<BEVE, ..>`
specialization (the first template argument matched when one of them has it,
else every class named to), at most QCAP definitions.

A definition's class is the innermost `struct|class Name [<args>] {..}` around
it, or the qualifier written on an out-of-class definition (`Cls<T>::fn(`).

usage: qual_edges.py <graph.json> <source tree>    (rewrites graph.json in place)"""
import collections, json, os, re, sys

QCAP = int(os.environ.get('CG_QCAP', 300))
path, src = sys.argv[1:3]
g = json.load(open(path))
nodes = g['nodes']

STRIP = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
ARGS = r'<(?:[^<>;{}]|<(?:[^<>;{}]|<[^<>;{}]*>)*>)*>'          # 3 levels of <..>
CLASS = re.compile(r'\b(?:struct|class)\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z_]\w*)\s*(' + ARGS + r')?'
                   r'\s*(?:final\s*)?(?::[^{;]*)?\{')
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*(' + ARGS + r')?\s*::\s*(?:template\s+)?([A-Za-z_]\w*)\s*(?:'
                  + ARGS + r')?\s*\(')
FIRST = re.compile(r'<\s*(?:\w+\s*::\s*)*([A-Za-z_]\w*)')


def clean(text):
    return STRIP.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text)


def first_arg(args):
    m = FIRST.match(args or '')
    return m.group(1) if m else None


by_file = collections.defaultdict(list)
for n in nodes:
    by_file[n['file']].append(n)

# (class, first template argument, method name) -> node ids
defs = collections.defaultdict(list)
for f, ns in by_file.items():
    try:
        text = clean(open(os.path.join(src, f), errors='replace').read())
    except OSError:
        continue
    line_at = [0]
    for i, ch in enumerate(text):
        if ch == '\n':
            line_at.append(i + 1)

    def line(off):
        lo, hi = 0, len(line_at) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if line_at[mid] <= off:
                lo = mid
            else:
                hi = mid - 1
        return lo + 1

    classes = []                                     # (first line, last line, name, first arg)
    for m in CLASS.finditer(text):
        depth, i = 0, m.end() - 1
        while i < len(text):
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            if depth == 0:
                break
            i += 1
        classes.append((line(m.start()), line(i), m.group(1), first_arg(m.group(2))))
    lines = text.split('\n')
    for n in ns:
        inner = [c for c in classes if c[0] <= n['line'] <= c[1]]
        if inner:
            c = max(inner, key=lambda c: c[0])
            defs[(c[2], c[3], n['name'])].append(n['id'])
            defs[(c[2], None, n['name'])].append(n['id'])
            continue
        head = ' '.join(lines[max(n['line'] - 1, 0):n['line'] + 2])
        m = re.search(r'\b([A-Za-z_]\w*)\s*(' + ARGS + r')?\s*::\s*~?' + re.escape(n['name']) + r'\s*\(', head)
        if m:
            defs[(m.group(1), first_arg(m.group(2)), n['name'])].append(n['id'])
            defs[(m.group(1), None, n['name'])].append(n['id'])

edges = {tuple(e) for e in g['edges']}
added = 0
for n in nodes:
    body = n.get('content') or ''
    if '{' not in body:
        continue
    for m in CALL.finditer(clean(body[body.find('{') + 1:])):
        cls, args, fn = m.groups()
        targets = (args and defs.get((cls, first_arg(args), fn))) or defs.get((cls, None, fn), ())
        if len(set(targets)) > QCAP:
            continue
        for t in set(targets):
            if t != n['id'] and (n['id'], t) not in edges:
                edges.add((n['id'], t))
                added += 1

g['edges'] = sorted(edges)
g['meta']['edges_qualified'] = added
json.dump(g, open(path, 'w'))
print(json.dumps({'qualified': added, 'classes_methods': len(defs)}))

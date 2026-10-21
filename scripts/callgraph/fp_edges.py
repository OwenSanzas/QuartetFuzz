#!/usr/bin/env python3
"""Address-taken edges: the calls through function pointers that Joern and
tree-sitter (direct calls only) cannot see.

  body      a function that names another function as a value, not a call
            (`s->alloc = miniz_def_alloc_func`, `qsort(.., cmp)`, `&Cls::fn`)
            gets an edge to it: it hands the pointer on, and the code that
            receives it calls it.
  table     a file-scope variable whose initializer names functions
            (zlib's configuration_table {.., deflate_fast}, yara's module table)
            (also through tables it names: libsodium's implementation pointer)
            gives every function that names the variable (as a value or indexed,
            configuration_table[level].func) an edge to each of them.

Only names in a value position count: after `= , ( ) & ? : {` or `return`
(`)` for casts and libsodium's SODIUM_C99(.field =) fn)
(through `&` and `Ns::` / `Cls<T>::` qualifiers), before `, ) ; } ] ? :`, and not inside
sizeof/alignof/decltype(...).  A type name (`tuple<int> x`, `Foo a;`) or a
call never matches.  Members (`x.f`, `p->f`) are fields and are skipped.
Macros a function uses count as its code (nested 3 deep): uriparser's
URI_CHECK_MEMORY_MANAGER(memory) assigns &defaultMemoryManager.
Names resolve like merge_final.py's name edges: definitions in the same file
first, else every definition when there are at most CG_CAP of them.

usage: fp_edges.py <graph.json> <source tree>    (rewrites graph.json in place)"""
import collections, json, os, re, sys

CAP = int(os.environ.get('CG_CAP', 25))
path, src = sys.argv[1:3]
g = json.load(open(path))
nodes = g['nodes']
by_name = collections.defaultdict(list)
for n in nodes:
    by_name[n['name']].append(n)

STRIP = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
# [&] [Ns::]* name, with what precedes and follows it
VALUE = re.compile(r'(\breturn\b|[=,(&?:{)])\s*&?\s*((?:[A-Za-z_]\w*\s*(?:<[^<>;{}()]*>\s*)?::\s*)*)([A-Za-z_]\w*)\s*(?=[,);}\]?]|:(?!:))')
WORD = re.compile(r'\b[A-Za-z_]\w*\b')
PREPROC = re.compile(r'^[ \t]*#(?:[^\n]*\\\n)*[^\n]*', re.M)    # with \-continued lines
SIZEOF = re.compile(r'\b(?:sizeof|alignof|decltype|offsetof)\s*\(\s*$')


def clean(text):
    return STRIP.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text)


def refs(text, cleaned=False):
    """Function-like names used as values in *text*."""
    t = text if cleaned else clean(text)
    out = set()
    i = 0
    while True:
        m = VALUE.search(t, i)
        if not m:
            return out
        i = m.end(3)
        before = t[:m.start(1)].rstrip()
        if m.group(1) == ':' and before.endswith(':'):       # the '::' of a qualifier
            continue
        if m.group(1) == '(' and SIZEOF.search(t[:m.start(1) + 1]):
            continue
        out.add(m.group(3))


def targets(name, file):
    defs = by_name.get(name, ())
    same = [d for d in defs if d['file'] == file]
    if same:
        return same
    return defs if len(defs) <= CAP else []


# function-like and object macros: a function that expands one uses what the
# macro body names (uriparser's URI_CHECK_MEMORY_MANAGER -> &defaultMemoryManager)
DEFINE = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)((?:[^\n]*\\\n)*[^\n]*)', re.M)
CODE = ('.c', '.cc', '.cpp', '.cxx', '.c++', '.h', '.hh', '.hpp', '.hxx', '.inc', '.ipp')
macros = collections.defaultdict(str)
for r, dn, fs in os.walk(src):
    dn[:] = [d for d in dn if not d.startswith('.')]
    for fn in fs:
        if fn.endswith(CODE):
            for m in DEFINE.finditer(clean(open(os.path.join(r, fn), errors='replace').read())):
                macros[m.group(1)] += '\n' + m.group(2).replace('\\\n', '\n')


def expand_macros(text, depth=3, seen=None):
    """The bodies of the macros *text* uses, nested up to *depth* levels."""
    seen = set() if seen is None else seen
    out = []
    for w in set(WORD.findall(text)) & macros.keys() - seen:
        seen.add(w)
        out.append(macros[w])
        if depth > 1:
            out.append(expand_macros(macros[w], depth - 1, seen))
    return '\n'.join(out)


# file-scope tables: variable name -> {file: names in its initializer}
raw = collections.defaultdict(lambda: collections.defaultdict(set))
spans = collections.defaultdict(list)
for n in nodes:
    spans[n['file']].append((n['line'], n['line_end']))
for f, sp in spans.items():
    try:
        lines = open(os.path.join(src, f), errors='replace').read().split('\n')
    except OSError:
        continue
    for a, b in sp:                      # blank out function bodies
        for i in range(max(a - 1, 0), min(b, len(lines))):
            lines[i] = ''
    text = clean('\n'.join(lines))
    text = PREPROC.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text)   # a #define's ';' and '='
    depth, start = 0, 0
    for i, ch in enumerate(text):
        if ch in '{([':
            depth += 1
        elif ch in '})]':
            depth -= 1
        elif ch == ';' and depth <= 0:
            stmt, start, depth = text[start:i], i + 1, 0
            if '=' not in stmt:
                continue
            lhs, rhs = stmt.split('=', 1)
            var = re.findall(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*$', lhs.strip())
            if var:
                # capstone: arch_configs[] = { CS_ARCH_CONFIG_AARCH64, .. } (macros)
                raw[var[0]][f] |= refs('=' + rhs + '\n' + expand_macros(rhs), cleaned=True)


def table(var, file):
    """*var*'s initializer as seen from *file*: that file's own, else the only
    file that has one."""
    per_file = raw.get(var, {})
    if file in per_file:
        return file, per_file[file]
    return next(iter(per_file.items())) if len(per_file) == 1 else (None, ())


def expand(var, file, seen):
    """Functions a table names, through the tables it names (libsodium's
    implementation = &..._donna_implementation = {.., fn, ..})."""
    f, names = table(var, file)
    if (var, f) in seen:
        return set()
    seen.add((var, f))
    out = {r for r in names if r in by_name}
    for r in names:
        if r in raw and r not in by_name:
            out |= expand(r, f, seen)
    return out


tables = collections.defaultdict(dict)       # variable -> {file: functions}
for var, per_file in raw.items():
    for f in per_file:
        fns = expand(var, f, set())
        if fns:
            tables[var][f] = fns

edges = {tuple(e) for e in g['edges']}
added = collections.Counter()
for n in nodes:
    body = n.get('content') or ''
    if not body:
        continue
    # skip the signature: names before the first '{' are the function's own
    body = body[body.find('{') + 1:] if '{' in body else ''
    body = clean(body)
    body += '\n' + expand_macros(body)
    for r in refs(body, cleaned=True):
        if r != n['name'] and r in by_name:
            for t in targets(r, n['file']):
                if (n['id'], t['id']) not in edges:
                    edges.add((n['id'], t['id']))
                    added['body'] += 1
    # a table is used however it is named: configuration_table[s->level].func
    for r in set(WORD.findall(body)) & tables.keys():
        per_file = tables[r]
        # another file's table only when no other file has one of that name
        names = per_file.get(n['file']) or (next(iter(per_file.values())) if len(per_file) == 1 else ())
        for fn in names:
            for t in targets(fn, n['file']):
                if (n['id'], t['id']) not in edges:
                    edges.add((n['id'], t['id']))
                    added['table'] += 1

g['edges'] = sorted(edges)
g['meta']['edges_addr_body'] = added['body']
g['meta']['edges_addr_table'] = added['table']
json.dump(g, open(path, 'w'))
print(json.dumps({'body': added['body'], 'table': added['table'], 'tables': len(tables)}))

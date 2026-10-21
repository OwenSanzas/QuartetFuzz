#!/usr/bin/env python3
"""How good is each static call graph, measured against what gold actually ran.

For every case, the functions covered by replaying gold's long-term
global_corpus (analysis/24h/sets/gold_ref/<case>/covsets.json.gz; the gold
harness file excluded) are dynamic ground truth: each of them really executes.

  target      the case's target function is a node of the graph
  node_recall share of gold-covered functions that are graph nodes
  edge_recall share of gold-covered functions (graph nodes) that have at least
              one gold-covered caller in the graph.  The misses are the APIs the
              harness calls directly (expected, a handful) plus calls the graph
              missed (function pointers, virtual dispatch, macros).
  reach       share of gold-covered functions (graph nodes) reachable from the
              target over call edges: what find_path / get_callees can lead the
              agent to.  Misses are other APIs the gold harness calls next to the
              target, plus code behind indirect calls.
  reach_h     the same from the target plus every function the gold harness
              calls directly: the call/jmp targets in the gold coverage binary of
              the harness file's functions (objdump; grading only).  What is
              left unreached is code the graph cannot lead to at all (function
              pointers, callbacks, dispatch tables).
  unreached   a sample of what reach_h leaves unreached.

Gold data is only used here to grade the graphs; QF never sees it.
Writes <callgraphs>/CHECK.tsv and prints it.
"""
import collections, glob, gzip, json, os, re, sqlite3, subprocess

E = '/home/ze/agf-public/experiment-data'
CG = os.environ.get('CG_CHECK_DIR', f'{E}/qf/callgraphs')
SETS = f'{E}/analysis/24h/sets/gold_ref'
GOLD = f'{E}/gold/bins_and_corpus'
cases = json.load(open('/home/ze/agf-public/qf-work/cases.json'))
index = json.load(open(f'{CG}/index.json'))   # the graph QF loads for each case


def demangle(names):
    out = subprocess.run(['c++filt'], input='\n'.join(names), capture_output=True, text=True).stdout.splitlines()
    return {n: d if d != n else mangled_last(n) for n, d in zip(names, out)}


def mangled_last(sym):
    """The last plain name of a symbol c++filt 14 cannot read (C++20
    constraints, `Tk...`): _ZN3glz11invalid_endITk...E... -> invalid_end.
    Stops at the first template argument list; operators are not decoded."""
    m = re.match(r'_ZN[KVRLO]*(?:St)?|_ZL?', sym)
    if not m:
        return sym
    i, last = m.end(), sym
    while i < len(sym) and sym[i].isdigit():
        j = i
        while sym[j].isdigit():
            j += 1
        last, i = sym[j:j + int(sym[i:j])], j + int(sym[i:j])
    return last


def base_name(sig):
    """'ns::Cls::fn(int) const' -> 'fn'; 'fn' -> 'fn'."""
    s = sig.replace('(anonymous namespace)', 'anon')
    s = re.sub(r'\[clone [^\]]*\]', '', s)
    while re.search(r'<[^<>]*>', s):
        s = re.sub(r'<[^<>]*>', '', s)
    s = re.sub(r'\s*(const|volatile|&|&&)?\s*$', '', s)
    if s.endswith(')'):   # drop the parameter list: the last balanced (...)
        depth = 0
        for i in range(len(s) - 1, -1, -1):
            depth += {')': 1, '(': -1}.get(s[i], 0)
            if depth == 0:
                s = s[:i]
                break
    return s.split('::')[-1].strip()


def harness_calls(cid, harness):
    """Base names of what the gold harness's functions call (None: no binary).
    The harness's functions are the binary's symbols whose debug line info is in
    the harness file (addr2line over nm's addresses; nm -l took hours on
    libvips), LLVMFuzzerTestOneInput at least.  Cached in CALLS_CACHE."""
    p, f = cid.split('/', 1)
    b = next(iter(glob.glob(f'{GOLD}/{p}/{f}/cov_build/*_cov')), None)
    if b is None:
        return None
    key = f'{b}:{os.path.getmtime(b)}'
    if key in calls_cache:
        return set(calls_cache[key])
    nm = subprocess.run(['nm', '--defined-only', b], capture_output=True, text=True).stdout
    text = [l.split() for l in nm.splitlines()]
    text = [(a, n) for a, t, n in (x for x in text if len(x) == 3) if t in 'tTwW']
    lines = subprocess.run(['llvm-addr2line', '-e', b], input='\n'.join('0x' + a for a, _ in text),
                           capture_output=True, text=True).stdout.splitlines()
    hb = os.path.basename(harness)
    syms = {n for (_, n), l in zip(text, lines) if os.path.basename(l.rsplit(':', 1)[0]) == hb}
    syms.add('LLVMFuzzerTestOneInput')
    out = set()
    for sym in syms:
        dis = subprocess.run(['objdump', '-d', '--no-show-raw-insn', f'--disassemble={sym}', b],
                             capture_output=True, text=True).stdout
        out |= set(re.findall(r'\b(?:call|jmp)\w*\s+[0-9a-f]+ <([^>+@]+)', dis))
    dm = demangle(sorted(out))
    calls_cache[key] = sorted({base_name(dm.get(t, t)) for t in out})
    json.dump(calls_cache, open(CALLS_CACHE, 'w'))
    return set(calls_cache[key])


CALLS_CACHE = '/home/ze/agf-public/qf-work/cg-inter/harness_calls.cache.json'
calls_cache = json.load(open(CALLS_CACHE)) if os.path.exists(CALLS_CACHE) else {}

rows = []
for cid, v in sorted(cases.items(), key=lambda kv: kv[1]['oss_fuzz_project']):
    c = v['oss_fuzz_project']
    db_path = f'{CG}/{index[cid]}'
    if not os.path.exists(db_path):
        rows.append(dict(case=c, graph='-'))
        continue
    db = sqlite3.connect(db_path)
    fns = db.execute('SELECT id, name, file FROM functions').fetchall()
    by_name = collections.defaultdict(set)
    by_file_name = collections.defaultdict(set)
    for i, n, f in fns:
        by_name[n].add(i)
        by_file_name[(f, n)].add(i)
    callers = collections.defaultdict(set)
    for a, b in db.execute('SELECT caller, callee FROM calls'):
        callers[b].add(a)

    d = json.load(gzip.open(f'{SETS}/{c}/covsets.json.gz'))
    harness = os.path.normpath(v['gold_path'])
    keys = [k for k in d['covered']['functions'] if not k.startswith(harness + ':')]
    raw = [k.rsplit(':', 1)[1] for k in keys]
    dm = demangle(sorted(set(raw)))
    covered_ids, missing = set(), []
    per_key = []
    for k, r in zip(keys, raw):
        path = k.split(':', 1)[0]
        rel = path.split('/', 3)[3] if path.count('/') >= 3 else path
        n = base_name(dm.get(r, r))
        ids = by_file_name.get((rel, n)) or by_name.get(n) or set()
        per_key.append(ids)
        covered_ids |= ids
        if not ids:
            missing.append(n)
    found = [ids for ids in per_key if ids]
    with_caller = sum(1 for ids in found if any(callers[i] & covered_ids for i in ids))
    callees = collections.defaultdict(set)
    for b, a_s in callers.items():
        for a in a_s:
            callees[a].add(b)
    def closure(start):
        seen, todo = set(start), list(start)
        while todo:
            u = todo.pop()
            for w in callees[u] - seen:
                seen.add(w)
                todo.append(w)
        return seen

    tgt = set(by_name.get(base_name(v['target_function']), ()))
    reached = [ids for ids in found if ids & closure(tgt)]
    hcalls = harness_calls(cid, harness)
    seen = closure(tgt | {i for n in hcalls or () for i in by_name.get(n, ())})
    reached_h = [ids for ids in found if ids & seen]
    unreached = sorted({fn for ids, k in zip(per_key, keys) if ids and not ids & seen
                        for fn in [base_name(dm.get(k.rsplit(':', 1)[1], k.rsplit(':', 1)[1]))]})
    rows.append(dict(case=c, graph=os.path.basename(db_path).split('.')[0], functions=len(fns),
                     calls=sum(len(s) for s in callers.values()),
                     target='Y' if base_name(v['target_function']) in by_name else 'N',
                     gold_funcs=len(keys), node_recall=f'{100 * len(found) / max(1, len(keys)):.0f}',
                     edge_recall=f'{100 * with_caller / max(1, len(found)):.0f}',
                     reach=f'{100 * len(reached) / max(1, len(found)):.0f}',
                     reach_h=f'{100 * len(reached_h) / max(1, len(found)):.0f}' if hcalls is not None else '?',
                     unreached=','.join(unreached[:8]),
                     missing_sample=','.join(sorted(set(missing))[:5])))

cols = ['case', 'graph', 'functions', 'calls', 'target', 'gold_funcs', 'node_recall', 'edge_recall', 'reach', 'reach_h',
        'missing_sample', 'unreached']
with open(f'{CG}/CHECK.tsv', 'w') as f:
    f.write('\t'.join(cols) + '\n')
    for r in rows:
        f.write('\t'.join(str(r.get(k, '')) for k in cols) + '\n')
for r in rows:
    print('\t'.join(str(r.get(k, '')) for k in cols))

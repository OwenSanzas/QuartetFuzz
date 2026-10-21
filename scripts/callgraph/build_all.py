#!/usr/bin/env python3
"""Build the call graph of every dataset project, one at a time, smallest source
tree first.  A project's graph is built from its first case's cleaned source
(build_graph.sh removes every harness, so all cases of a project give the same
graph).  A build starts only when MemAvailable >= 16 GB + the Joern heap.

Also writes <callgraphs>/index.json, case id -> graph file, which
harness/checker/callgraph.py reads.

usage: build_all.py [--skip p1,p2] [--only p1,p2] [--heap GB]"""
import argparse, json, os, subprocess, time

SRC = '/home/ze/agf-public/qf-work/src'
OUT = '/home/ze/agf-public/experiment-data/qf/callgraphs'
HERE = os.path.dirname(os.path.abspath(__file__))
TOOLDIRS = {'aflplusplus', 'fuzztest', 'honggfuzz', 'libfuzzer'}
# Trees too big to graph whole: only the libraries the case's fuzzer links (its
# build.sh link line), passed to build_graph.sh as CG_SUBDIRS.
SCOPE = {
    'binutils-c1': 'bfd opcodes libctf libsframe libiberty zlib include',
    'gnutls-c1': 'lib gl',                 # make -C gl; make -C lib (devel/ vendors openssl, boringssl)
    'boost-c1': 'libs/graph',              # libboost_graph.a
    'boost-c2': 'libs/program_options',    # libboost_program_options.a
    'boost-c3': 'libs/regex',              # libs/regex/src/*.cpp
}
# header-only; its tests/ and benchmarks/ ran c2cpg out of heap
SCOPE.update({f'glaze-c{i}': 'include' for i in range(1, 6)})

ap = argparse.ArgumentParser()
ap.add_argument('--skip', default='')
ap.add_argument('--only', default='')
ap.add_argument('--heap', type=int, default=8)
a = ap.parse_args()

def src_dir(v):
    """The project source dir under qf-work/src/<case>/: the dir the gold harness
    lives in (binutils-gdb; capstonenext vs capstonev5), else the dir named after the
    project (gold directly under /src: flac-c3, libevent), else the only non-tool dir.
    Dependencies checked out next to it (gmp, nettle, libpng, openssl, ...) are not
    graphed: they are outside the case's coverage scope too."""
    case = v['oss_fuzz_project']
    ds = [d for d in os.listdir(f'{SRC}/{case}') if os.path.isdir(f'{SRC}/{case}/{d}') and d not in TOOLDIRS]
    g = v['gold_path'].split('/')[2]
    for d in (g, v['project']):
        if d in ds:
            return d
    if len(ds) == 1:
        return ds[0]
    raise SystemExit(f'{case}: cannot tell the project source dir among {ds}')


# One graph per (project, source dir), from its first case.  It is named after the
# project, or after the case when a project has several source dirs.
cases = json.load(open('/home/ze/agf-public/qf-work/cases.json'))
groups, case_group = {}, {}
for cid, v in sorted(cases.items(), key=lambda kv: kv[1]['oss_fuzz_project']):
    key = (v['project'], src_dir(v), SCOPE.get(v['oss_fuzz_project'], ''))
    groups.setdefault(key, v['oss_fuzz_project'])
    case_group[cid] = key
per_project = {}
for p, d, _ in groups:
    per_project[p] = per_project.get(p, 0) + 1


def graph_name(key):
    return key[0] if per_project[key[0]] == 1 else groups[key]


index = {cid: f'{graph_name(k)}.callgraph.sqlite' for cid, k in sorted(case_group.items())}
os.makedirs(OUT, exist_ok=True)
with open(f'{OUT}/index.json', 'w') as f:
    json.dump(index, f, indent=1, sort_keys=True)


def size(case, d):
    return int(subprocess.run(['du', '-s', '--exclude=.git', f'{SRC}/{case}/{d}'], capture_output=True, text=True).stdout.split()[0])


skip = set(filter(None, a.skip.split(',')))
only = set(filter(None, a.only.split(',')))
jobs = []
for (p, d, sc), c in groups.items():
    if p in skip or (only and p not in only):
        continue
    jobs.append((size(c, d), graph_name((p, d, sc)), c, d, sc))
jobs.sort()
print(f'{len(jobs)} projects', flush=True)
for kb, p, c, d, sc in jobs:
    while int(open('/proc/meminfo').read().split('MemAvailable:')[1].split()[0]) < (16 + a.heap) * 1048576:
        time.sleep(30)
    t0 = time.time()
    r = subprocess.run([f'{HERE}/build_graph.sh', p, c, d], capture_output=True, text=True,
                       env=dict(os.environ, CG_HEAP=str(a.heap), CG_SUBDIRS=sc))
    last = (r.stdout.strip().splitlines() or [r.stderr.strip()[-200:]])[-1]
    print(f'{time.strftime("%T")} {p} ({kb // 1024} MB) rc={r.returncode} {int(time.time() - t0)}s {last}', flush=True)
print('ALL DONE')

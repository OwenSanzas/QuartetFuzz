#!/usr/bin/env python3
"""Add the build-generated C/C++ files a project compiles to its cleaned source
tree before parsing: llhttp's build/c/llhttp.c (from TypeScript), libidn2's gl/
(gnulib, pulled in by bootstrap), configure's config.h, ...  Without them the
graph misses code that is in the fuzz binary.

The generated files come from QF's own build of the case (the /src snapshot in
finals/all100/<case>/final_cov/out/src, taken after QF's build.sh ran), never
from gold.  Only files absent from the clean tree are added:
  sources   .c/.cc/.cpp/... that were compiled: an object of the same stem
            next to it (x.o, libtool's libgnu_la-x.o/.lo) or a CMake object
            <bindir>/CMakeFiles/<t>.dir/<p>.o for the source <bindir>/<p>.  libidn2's gnulib/ checkout and gnutls' src/gl
            hold sources that were never built and stay out.
  headers   generated .h/... in a directory that holds source code (clean tree
            or added above), or #include'd by name from such source when that
            name is unique among generated headers and absent from the clean
            tree (llhttp's build/llhttp.h); link farms (boost/), dependency
            checkouts (_deps/, gnulib/) and node_modules/ stay out.
  never     anything inside a dependency fetched at build time: a path through
            _deps/, third_party/, 3rdparty/, external/ or vendor/ whose next
            directory is not in the clean tree (sentencepiece's absl).
Harness files among them are removed afterwards by build_graph.sh like any other.

With CG_SUBDIRS set (see build_graph.sh) only files under those subdirectories.

usage: overlay_generated.py <case> <source dir> <dest tree>   (prints added files)"""
import collections, os, re, shutil, sys

FIN = '/home/ze/agf-public/experiment-data/qf/finals/all100'
SRC = '/home/ze/agf-public/qf-work/src'
SOURCES = ('.c', '.cc', '.cpp', '.cxx', '.c++')
HEADERS = ('.h', '.hh', '.hpp', '.hxx', '.inc', '.ipp')
SKIP = {'node_modules', '.git'}
DEPS = {'_deps', 'third_party', '3rdparty', 'external', 'vendor'}

case, sdir, dest = sys.argv[1:4]
built = f'{FIN}/{case}/final_cov/out/src/{sdir}'
clean = f'{SRC}/{case}/{sdir}'
if not os.path.isdir(built):
    sys.exit(0)



def walk(root):
    for r, dn, fs in os.walk(root):
        dn[:] = [d for d in dn if d not in SKIP]
        for f in fs:
            yield os.path.relpath(os.path.join(r, f), root)


src_dirs = {os.path.dirname(rel) for rel in walk(clean) if rel.endswith(SOURCES)}


def fetched_dep(rel):
    parts = rel.split('/')
    return any(p in DEPS and not os.path.isdir(f'{clean}/' + '/'.join(parts[:i + 2]))
               for i, p in enumerate(parts[:-2]))


new = [rel for rel in walk(built) if rel.endswith(SOURCES + HEADERS)
       and not os.path.exists(f'{clean}/{rel}') and not fetched_dep(rel)]
objs = [rel for rel in walk(built) if rel.endswith(('.o', '.lo', '.obj'))]
obj_in_dir = collections.defaultdict(set)
for o in objs:
    obj_in_dir[os.path.dirname(o)].add(os.path.basename(o).rsplit('.', 1)[0])
cmake_srcs = set()
for o in objs:
    m = re.match(r'(?:(.*)/)?CMakeFiles/[^/]+\.dir/(.+)\.o$', o)
    if m:
        cmake_srcs.add(os.path.normpath(os.path.join(m.group(1) or '', m.group(2))))


def compiled(rel):
    d, f = os.path.split(rel)
    stem = f.rsplit('.', 1)[0]
    if any(n == stem or n.endswith('-' + stem) for n in obj_in_dir[d]):
        return True
    return rel in cmake_srcs


add = {rel for rel in new if rel.endswith(SOURCES) and compiled(rel)}
src_dirs |= {os.path.dirname(rel) for rel in add}
included = set()
for rel in [r for r in walk(clean) if r.endswith(SOURCES + HEADERS)] + sorted(add):
    root = clean if os.path.exists(f'{clean}/{rel}') else built
    for m in re.finditer(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', open(f'{root}/{rel}', errors='replace').read(), re.M):
        included.add(os.path.basename(m.group(1)))
clean_hdr = {os.path.basename(r) for r in walk(clean) if r.endswith(HEADERS)}
new_hdr = [rel for rel in new if rel.endswith(HEADERS)]
n_hdr = collections.Counter(os.path.basename(rel) for rel in new_hdr)
add |= {rel for rel in new_hdr if os.path.dirname(rel) in src_dirs
        or (os.path.basename(rel) in included and n_hdr[os.path.basename(rel)] == 1
            and os.path.basename(rel) not in clean_hdr)}
scope = os.environ.get('CG_SUBDIRS', '').split()
if scope:
    add = {rel for rel in add if any(rel.startswith(d.rstrip('/') + '/') for d in scope)}
for rel in sorted(add):
    os.makedirs(os.path.dirname(f'{dest}/{rel}') or dest, exist_ok=True)
    shutil.copy2(f'{built}/{rel}', f'{dest}/{rel}')
    print(rel)

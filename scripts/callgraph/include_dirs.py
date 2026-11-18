#!/usr/bin/env python3
"""Include directories for joern-parse, found from the tree itself.

Without them c2cpg cannot open headers included as <uriparser/Uri.h> or
"llhttp.h" from another directory, so macros defined there stay unexpanded:
uriparser's URI_FUNC(ParseUri) -> uriParseUriA gave 190 functions and 12 edges
instead of 608 and 988.

For every #include "p" / <p> in a source or header that does not resolve next to
the including file, each directory D in the tree with D/p a file is a search
directory.  Includes of system headers match nothing and add nothing.

usage: include_dirs.py <tree>      prints one absolute directory per line"""
import collections, os, re, sys

root = os.path.abspath(sys.argv[1])
CODE = ('.c', '.cc', '.cpp', '.cxx', '.c++', '.h', '.hh', '.hpp', '.hxx', '.inc', '.ipp')
INC = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)

files = []
for r, dn, fs in os.walk(root):
    dn[:] = [d for d in dn if not d.startswith('.') and d != 'node_modules']
    files += [os.path.join(r, f) for f in fs if f.endswith(CODE)]
by_base = collections.defaultdict(list)
for f in files:
    by_base[os.path.basename(f)].append(f)

dirs = set()
for f in files:
    for p in set(INC.findall(open(f, errors='replace').read())):
        p = os.path.normpath(p)
        if p.startswith('..') or os.path.isfile(os.path.join(os.path.dirname(f), p)):
            continue
        for g in by_base.get(os.path.basename(p), ()):
            if g.endswith('/' + p):
                dirs.add(g[:-len(p) - 1])
print('\n'.join(sorted(dirs)))

#!/usr/bin/env python3
"""Remove the fuzz harnesses from a source tree.

A file that defines LLVMFuzzerTestOneInput is deleted, unless the harness sits
in `#if[def] ...FUZZ...` blocks of a library file (util-linux's
libfdisk/src/script.c: `#ifdef FUZZ_TARGET ... LLVMFuzzerTestOneInput ...
#endif`): then only those blocks are blanked and the library code stays.

usage: strip_harness.py <tree>     prints "deleted <rel>" / "stripped <rel>" lines"""
import os, re, sys

root = sys.argv[1]
CODE = ('.c', '.cc', '.cpp', '.cxx', '.c++')
OPEN = re.compile(r'^\s*#\s*if')
END = re.compile(r'^\s*#\s*endif')
FUZZ_IF = re.compile(r'^\s*#\s*if(?:def|ndef)?\b.*FUZZ', re.I)


def strip_fuzz_blocks(lines):
    out, depth = [], 0
    for line in lines:
        if depth:
            depth += bool(OPEN.match(line)) - bool(END.match(line))
            out.append('\n' if line.endswith('\n') else '')     # keep line numbers
        elif FUZZ_IF.match(line) and not line.lstrip().startswith('#ifndef'):
            depth = 1
            out.append('\n')
        else:
            out.append(line)
    return out


for r, dn, fs in os.walk(root):
    for f in sorted(fs):
        if not f.endswith(CODE):
            continue
        p = os.path.join(r, f)
        lines = open(p, errors='replace').readlines()
        if not any('LLVMFuzzerTestOneInput' in l for l in lines):
            continue
        rel = os.path.relpath(p, root)
        kept = strip_fuzz_blocks(lines)
        if not any('LLVMFuzzerTestOneInput' in l for l in kept) and \
                sum(bool(l.strip()) for l in kept) > 0.5 * sum(bool(l.strip()) for l in lines):
            open(p, 'w').writelines(kept)
            print('stripped', rel)
        else:
            os.remove(p)
            print('deleted', rel)

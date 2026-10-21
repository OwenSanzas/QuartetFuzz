#!/usr/bin/env python3
"""graph.json (merge_final.py) -> <project>.callgraph.sqlite, the file the QF
call-graph tools (get_callers / get_callees / find_path) read.

Same tables as fbbench's build_sqlite.py minus the harness-entry columns: QF
writes the harness, so there is no entry to measure depth from.

  meta(key, value)
  functions(id, uid 'name@file:line', name, file, line, line_end)
  calls(caller, callee)            functions.id

Harness files were deleted before parsing; any node still in a removed file is
an error, not something to filter quietly.

usage: to_sqlite.py <graph.json> <out.sqlite> <removed_harness.txt>
"""
import json, os, sqlite3, sys, time

graph, out, removed = sys.argv[1:4]
g = json.load(open(graph))
gone = {l.strip() for l in open(removed) if l.strip()}
# A bodiless prototype of a libFuzzer hook (lz4's ossfuzz/fuzz.h) carries no code
# and no edges: drop it.  A hook with a body is harness code left in the tree.
# The libFuzzer hooks; capstone's own LLVMFuzzerInit / LLVMFuzzerReturnOneInput
# (suite/fuzz/fuzz_llvm.cpp, a comparison driver) are ordinary project code.
HOOKS = {"LLVMFuzzerTestOneInput", "LLVMFuzzerInitialize", "LLVMFuzzerCustomMutator",
         "LLVMFuzzerCustomCrossOver"}
linked = {x for e in g["edges"] for x in e}
proto = {n["id"] for n in g["nodes"] if n["name"] in HOOKS
         and n["line"] == n["line_end"] and n["id"] not in linked}
g["nodes"] = [n for n in g["nodes"] if n["id"] not in proto]
bad = [n["id"] for n in g["nodes"] if n["file"] in gone or n["name"] in HOOKS]
if bad:
    sys.exit(f"harness code in the graph: {bad[:5]}")

if os.path.exists(out):
    os.remove(out)
db = sqlite3.connect(out)
db.executescript("""
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE functions(id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, name TEXT NOT NULL,
                       file TEXT NOT NULL, line INTEGER NOT NULL, line_end INTEGER NOT NULL);
CREATE TABLE calls(caller INTEGER NOT NULL, callee INTEGER NOT NULL);
""")
ids = {}
for i, n in enumerate(sorted(g["nodes"], key=lambda n: (n["file"], n["line"], n["name"])), 1):
    ids[n["id"]] = i
    db.execute("INSERT INTO functions VALUES (?,?,?,?,?,?)", (i, n["id"], n["name"], n["file"], n["line"], n["line_end"]))
db.executemany("INSERT INTO calls VALUES (?,?)", [(ids[s], ids[t]) for s, t in g["edges"] if s in ids and t in ids])
db.executescript("""
CREATE INDEX functions_name ON functions(name);
CREATE INDEX functions_file ON functions(file);
CREATE INDEX calls_caller ON calls(caller, callee);
CREATE INDEX calls_callee ON calls(callee, caller);
""")
m = g["meta"]
meta = {"project": m["challenge"], "method": m["method"], "functions": len(ids),
        "calls": db.execute("SELECT count(*) FROM calls").fetchone()[0],
        "edges_resolved": m["edges_resolved"], "edges_byname": m["edges_byname"],
        "removed_harness_files": len(gone), "built": time.strftime("%Y-%m-%dT%H:%M:%S")}
db.executemany("INSERT INTO meta VALUES (?,?)", [(k, str(v)) for k, v in meta.items()])
db.commit()
print(json.dumps(meta))

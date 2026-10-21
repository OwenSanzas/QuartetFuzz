#!/bin/bash
# Static call graph of one project for the QF +Static ablation, built the way
# fbbench's callgraph-tools does it (afc-crs-.../fbbench/callgraph-tools):
#   Joern (resolved call edges + name edges) ∪ tree-sitter name edges
#   -> merge_final.py (resolved edges kept; a name edge links to every
#      same-named definition when there are at most CG_CAP of them)
#   -> fp_edges.py (address-taken edges: function pointers, dispatch tables)
#   -> qual_edges.py (C++ Cls<A>::fn(..) calls to fn in class Cls, no CG_CAP)
#   -> to_sqlite.py
#
# Source: the case's cleaned source tree (qf-work/src/<case>/<project>), with
# .git and every fuzz harness file (any file defining LLVMFuzzerTestOneInput)
# removed first (strip_harness.py: a library file with the harness in #ifdef
# FUZZ_TARGET blocks, util-linux's libfdisk/src/script.c, loses only those), so neither the gold harness of this case nor those of the
# project's other cases enter the graph.  The cases of one project differ only
# in which gold harness is stubbed, so one graph per project.
#
# Two additions to fbbench, both so the graph covers what the fuzz binary runs:
#   overlay_generated.py  build-generated sources/headers from QF's own build of
#                         the case (llhttp's build/c/llhttp.c, libidn2's gnulib
#                         gl/, config.h), added before harness removal
#   include_dirs.py       --include directories found from the tree's own
#                         #include lines, so header macros expand (uriparser)
#
# CG_SUBDIRS="a b/c" limits the tree to those subdirectories of the source dir
# (and the overlay to files under them): the libraries the fuzzer links, for the
# trees too big to graph whole (binutils-gdb, boost, gnutls).
#
# usage: build_graph.sh <project> <case> [<source dir under qf-work/src/<case>, default project>]
set -u
SP="$(dirname "$(readlink -f "$0")")"
J=/home/ze/bin/joern/joern-cli
SRC=/home/ze/agf-public/qf-work/src
INTER=/home/ze/agf-public/qf-work/cg-inter
OUT=/home/ze/agf-public/experiment-data/qf/callgraphs
proj="$1"; case="$2"; sdir="${3:-$1}"
mkdir -p "$OUT" "$INTER"
[ -s "$OUT/$proj.callgraph.sqlite" ] && { echo "[$proj] exists, skipped"; exit 0; }
w="$INTER/$proj"
# a work dir copied under another CG_SUBDIRS is stale
[ "$(cat "$w/scope" 2>/dev/null)" = "${CG_SUBDIRS:-}" ] || rm -rf "${w:?}/src" "${w:?}/graph_c.jsonl"
if [ ! -d "$w/src" ]; then
  rm -rf "${w:?}"; mkdir -p "$w"
  echo "${CG_SUBDIRS:-}" > "$w/scope"
  if [ -n "${CG_SUBDIRS:-}" ]; then
    mkdir -p "$w/src"
    (cd "$SRC/$case/$sdir" && rsync -aR --exclude=.git $CG_SUBDIRS "$w/src/") || { echo "[$proj] copy failed"; exit 1; }
  else
    rsync -a --exclude=.git "$SRC/$case/$sdir/" "$w/src/" || { echo "[$proj] copy failed"; exit 1; }
  fi
  chmod -R u+w "$w/src"
  python3 "$SP/overlay_generated.py" "$case" "$sdir" "$w/src" > "$w/overlay.txt" || { echo "[$proj] overlay failed"; exit 1; }
  python3 "$SP/strip_harness.py" "$w/src" > "$w/harness.txt" || { echo "[$proj] harness removal failed"; exit 1; }
  sed -n 's/^deleted //p' "$w/harness.txt" | sort > "$w/removed_harness.txt"
  for ext in c cc cpp; do find "$w/src" -name "*.$ext.in" | while read -r f; do cp "$f" "${f%.in}"; done; done
fi
python3 "$SP/include_dirs.py" "$w/src" > "$w/include_dirs.txt" || { echo "[$proj] include dirs failed"; exit 1; }
inc=()
while read -r d; do inc+=(--include "$d"); done < "$w/include_dirs.txt"
fe=()
[ ${#inc[@]} -gt 0 ] && fe=(--frontend-args "${inc[@]}")
export JAVA_OPTS="-Xmx${CG_HEAP:-12}g"
python3 "$SP/ts_cg.py" "$w/src" "$w/graph_ts.jsonl" cpp > "$w/ts.log" 2>&1 || { echo "[$proj] tree-sitter failed"; exit 4; }
# CG_NO_JOERN=1: tree-sitter functions and name edges only (glaze: c2cpg ran
# out of a 12 GB heap on its templates)
[ -n "${CG_NO_JOERN:-}" ] && grep '"kind": "func"' "$w/graph_ts.jsonl" > "$w/graph_c.jsonl"
if [ ! -s "$w/cpg" ] && [ ! -s "$w/graph_c.jsonl" ]; then
  timeout "${CG_PARSE_TIMEOUT:-3600}" "$J/joern-parse" "$w/src" -o "$w/cpg" --language c \
    --max-num-def "${CG_MAXDEF:-2000}" "${fe[@]}" > "$w/parse.log" 2>&1
fi
[ -s "$w/cpg" ] || [ -s "$w/graph_c.jsonl" ] || { echo "[$proj] joern-parse failed/timed out"; tail -2 "$w/parse.log"; exit 2; }
if [ ! -s "$w/graph_c.jsonl" ]; then
  timeout "${CG_EXPORT_TIMEOUT:-3600}" "$J/joern" --script "$SP/callgraph_combined.sc" \
    --param cpgPath="$w/cpg" --param outputPath="$w/graph_c.jsonl" > "$w/run.log" 2>&1
fi
[ -s "$w/graph_c.jsonl" ] || { echo "[$proj] joern export failed/timed out"; tail -3 "$w/run.log"; exit 3; }
cat "$w/graph_c.jsonl" > "$w/graph_u.jsonl"
grep '"kind": "edge"' "$w/graph_ts.jsonl" >> "$w/graph_u.jsonl"
CG_CAP="${CG_CAP:-25}" python3 "$SP/merge_final.py" "$w/graph_u.jsonl" "$w/src" "$w/graph.json" cpp "$proj" > "$w/merge.log" 2>&1 \
  || { echo "[$proj] merge failed"; tail -3 "$w/merge.log"; exit 5; }
CG_CAP="${CG_CAP:-25}" python3 "$SP/fp_edges.py" "$w/graph.json" "$w/src" > "$w/fp_edges.log" 2>&1 \
  || { echo "[$proj] fp_edges failed"; tail -3 "$w/fp_edges.log"; exit 7; }
python3 "$SP/qual_edges.py" "$w/graph.json" "$w/src" > "$w/qual_edges.log" 2>&1 \
  || { echo "[$proj] qual_edges failed"; tail -3 "$w/qual_edges.log"; exit 8; }
python3 "$SP/to_sqlite.py" "$w/graph.json" "$OUT/$proj.callgraph.sqlite" "$w/removed_harness.txt" \
  || { echo "[$proj] sqlite failed"; exit 6; }
cp "$w/removed_harness.txt" "$OUT/$proj.removed_harness.txt"
sed -n 's/^stripped //p' "$w/harness.txt" | sort > "$OUT/$proj.stripped_harness.txt"
cp "$w/overlay.txt" "$OUT/$proj.overlay.txt"
sed "s#^$w/src#.#" "$w/include_dirs.txt" > "$OUT/$proj.include_dirs.txt"
rm -f "$w/cpg"
echo "[$proj] -> $OUT/$proj.callgraph.sqlite"

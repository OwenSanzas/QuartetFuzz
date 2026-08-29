#!/usr/bin/env bash
# Coverage over the project's global OSS-Fuzz corpus, replayed deterministically.
# This is corpus replay, not fuzzing: -merge=1 is the procedure OSS-Fuzz itself
# uses for its coverage reports, and it tolerates inputs that crash the target.
set -uo pipefail
cd "$(dirname "$0")"

BIN=sig_fuzz_cov
IMG=gcr.io/oss-fuzz-base/base-runner:ubuntu-24-04
SRC_SCOPE=/src/hpn-ssh/
mkdir -p report

[ -f "cov_build/$BIN" ]    || { echo "no coverage build at cov_build/$BIN" >&2; exit 2; }
[ -d global_corpus/seeds ] || { echo "no corpus at global_corpus/seeds" >&2; exit 2; }
SEEDS=$(find global_corpus/seeds -type f | wc -l)
echo "measuring coverage over $SEEDS seeds with $BIN (scope: $SRC_SCOPE)"

# The scoping step lives in its own file: nesting quoted Python inside a quoted
# `bash -c` inside a shell heredoc loses the escapes.
cat > report/_scope.py <<'PYEOF'
import json, os, sys
scope = os.environ["SRC_SCOPE"]
d = json.load(open("/report/coverage_export.json"))["data"][0]
METRICS = ("lines", "functions", "regions", "branches")

def totals(files):
    out = {}
    for m in METRICS:
        c = sum(f["summary"][m]["covered"] for f in files if m in f["summary"])
        t = sum(f["summary"][m]["count"]   for f in files if m in f["summary"])
        out[m] = {"covered": c, "count": t,
                  "percent": round(100 * c / t, 3) if t else 0.0}
    return out

own = [f for f in d["files"] if f["filename"].startswith(scope)]
json.dump({"scoped": totals(own), "all_linked": totals(d["files"]),
           "files_in_scope": len(own), "files_total": len(d["files"]),
           "scope": scope,
           "prefixes": sorted({"/".join(f["filename"].split("/")[:3])
                               for f in d["files"]})[:20]},
          open("/report/coverage_scoped.json", "w"), indent=1)
PYEOF

docker run --rm \
  -v "$PWD/cov_build:/out" \
  -v "$PWD/global_corpus/seeds:/corpus:ro" \
  -v "$PWD/report:/report" \
  -e FUZZING_ENGINE=libfuzzer -e SANITIZER=coverage -e HELPER=True \
  -e SRC_SCOPE="$SRC_SCOPE" -e BIN="$BIN" \
  "$IMG" \
  /bin/bash -c '
    set -u
    mkdir -p /tmp/dumps /tmp/empty
    export LLVM_PROFILE_FILE=/tmp/dumps/cov.%1m.profraw
    /out/$BIN -merge=1 -timeout=100 /tmp/empty /corpus > /report/cov_run.log 2>&1
    echo "target exit: $?" >> /report/cov_run.log
    llvm-profdata merge -sparse /tmp/dumps/*.profraw -o /tmp/cov.profdata 2>&1 | tail -2
    # -summary-only omits the per-line segment arrays, which reach ~1 GB on the
    # larger targets. The per-file summaries it keeps are what scoping needs.
    llvm-cov export -summary-only -instr-profile=/tmp/cov.profdata /out/$BIN \
      > /report/coverage_export.json 2>/report/llvmcov_err.log
    echo "export exit: $? size: $(stat -c %s /report/coverage_export.json)"
    python3 /report/_scope.py && echo "scope ok"
  ' 2>&1 | tee report/run_cov.log

python3 - <<'PYEOF'
import json, os, time
p = "report/coverage_scoped.json"
if not os.path.exists(p) or os.path.getsize(p) == 0:
    print("no coverage produced"); raise SystemExit(0)
d = json.load(open(p))
res = dict(d["scoped"])
res.update({k: d[k] for k in ("scope", "files_in_scope", "files_total")})
res["all_linked"] = d["all_linked"]
res["timestamp"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
res["deterministic"] = True
res["method"] = "-merge=1 replay, llvm-cov -summary-only, scoped to project sources"
json.dump(res, open("report/coverage.json", "w"), indent=1)
if not d["files_in_scope"]:
    print(f"  no files under {d['scope']} (total {d['files_total']})")
    print("  prefixes seen:", ", ".join(d["prefixes"][:8]))
else:
    for m in ("lines", "functions", "regions", "branches"):
        v = res[m]; print("  %-10s %8d/%-8d %6.2f%%" % (m, v["covered"], v["count"], v["percent"]))
    print(f"  scope {d['scope']}: {d['files_in_scope']}/{d['files_total']} files")
PYEOF

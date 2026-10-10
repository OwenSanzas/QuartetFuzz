#!/usr/bin/env python3
"""Gold-territory coverage (recall) and extra coverage of a run against a reference.
usage: recall.py <run_covsets.json.gz> <ref_covsets.json.gz>   -> JSON on stdout
       recall.py --batch <campaign>...                         -> per-trial recall vs the case's
                                                                 historical corpus, written to
                                                                 <trial>/cov/recall_hist.json"""
import sys, gzip, json, glob, os
HERE = os.path.dirname(os.path.abspath(__file__)); DS = os.path.dirname(HERE)
def load(p): return json.load(gzip.open(p))
def recall(run, ref):
    out = {"reference": ref["corpus"], "run": run["corpus"]}
    for m in ("lines", "functions", "branches"):
        R, G = set(run["covered"][m]), set(ref["covered"][m])
        out[m] = {"gold": len(G), "run": len(R), "both": len(R & G), "recall": (len(R & G) / len(G)) if G else None,
                  "containment": (len(R & G) / len(R)) if R else None,
                  "extra": len(R - G), "missed": len(G - R), "universe": run["totals"][m]}
    return out
if sys.argv[1] == "--batch":
    for camp in sys.argv[2:]:
        for trial in sorted(glob.glob(f"{camp}/*/*/trial_*")):
            proj, fz = trial.split('/')[-3], trial.split('/')[-2]
            rp, hp = f"{trial}/cov/sets_24h/covsets.json.gz", f"{DS}/projects/{proj}/{fz}/report/covsets_hist/covsets.json.gz"
            if not (os.path.exists(rp) and os.path.exists(hp)): continue
            r = recall(load(rp), load(hp)); json.dump(r, open(f"{trial}/cov/recall_hist.json", "w"), indent=1)
            print(f"{proj}/{fz:40s} recall L {r['lines']['recall']:.3f} F {r['functions']['recall']:.3f} B {r['branches']['recall']:.3f} | containment L {r['lines']['containment']:.3f} F {r['functions']['containment']:.3f} B {r['branches']['containment']:.3f} | extra_lines {r['lines']['extra']}")
else:
    print(json.dumps(recall(load(sys.argv[1]), load(sys.argv[2])), indent=1))

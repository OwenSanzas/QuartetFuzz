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

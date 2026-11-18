# 最终:解析边(精确) ∪ 名字边(末名连到所有同名定义,扇出<=CAP);CAP 防常见名爆炸
import json,sys,os,collections,re
graph,srcroot,out,lang,ch=sys.argv[1:6]
CAP=int(os.environ.get("CG_CAP","25"))
JUNK={"<global>","<includes>","<clinit>","<fakeNew>","<module>"}
ENTRY=("LLVMFuzzerTestOneInput",) if lang!="jvm" else ("fuzzerTestOneInput",)
base=lambda p:p.split("/")[-1]; fid=lambda n,f,l:f"{n}@{base(f)}:{l}"
last=lambda n:re.split(r'[.:]',n)[-1] if n else n
nodes={}; redges=[]; nedges=[]
for l in open(graph):
    try:e=json.loads(l)
    except:continue
    k=e["kind"]
    if k=="func" and e["name"] not in JUNK:
        i=fid(e["name"],e["file"],e["line"]); nodes.setdefault(i,{"name":e["name"],"file":e["file"],"line":e["line"],"line_end":e["line_end"]})
    elif k=="redge": redges.append(e)
    elif k=="edge": nedges.append(e)
bylast=collections.defaultdict(list); byid=set(nodes)
for i,r in nodes.items(): bylast[last(r["name"])].append(i)
eset=set(); nres=0; nname=0
# 解析边:精确 fid;不中则按 名字@同文件;再不中按末名(<=CAP)
for e in redges:
    s=fid(e["caller"],e["cfile"],e["cline"])
    if s not in byid: continue
    t=fid(e["callee"],e["efile"],e["eline"])
    if t in byid:
        if t!=s and (s,t) not in eset: eset.add((s,t)); nres+=1
    else:
        cand=[i for i in bylast.get(last(e["callee"]),[]) if base(nodes[i]["file"])==base(e["efile"])] or bylast.get(last(e["callee"]),[])
        if 0<len(cand)<=CAP:
            for t in cand:
                if t!=s and (s,t) not in eset: eset.add((s,t)); nres+=1
# 名字边:末名所有同名(<=CAP);兼容 joern(cfile/cline)与 tree-sitter(file,按名+文件解析 caller)
byname_full=collections.defaultdict(list)
for i,r in nodes.items(): byname_full[r["name"]].append(i)
def caller_fid(e):
    if "cfile" in e:
        return fid(e["caller"],e["cfile"],e["cline"])
    cand=byname_full.get(e["caller"],[])
    if len(cand)==1: return cand[0]
    same=[i for i in cand if base(nodes[i]["file"])==base(e.get("file",""))]
    return same[0] if same else None
for e in nedges:
    s=caller_fid(e)
    if not s or s not in byid: continue
    cand=bylast.get(last(e["callee"]),[])
    if 0<len(cand)<=CAP:
        for t in cand:
            if t!=s and (s,t) not in eset: eset.add((s,t)); nname+=1
edges=sorted(eset)
adj=collections.defaultdict(list)
for s,t in edges: adj[s].append(t)
cache={}
def lines(relf):
    if relf in cache:return cache[relf]
    for p in (os.path.join(srcroot,relf),os.path.join(srcroot,base(relf))):
        if os.path.exists(p):
            try:cache[relf]=open(p,errors="replace").read().splitlines();return cache[relf]
            except OSError:pass
    cache[relf]=None;return None
def sl(relf,a,b):
    ls=lines(relf);return "\n".join(ls[a-1:min(b,len(ls))]) if ls and a>=1 else ""
ents=[i for i,r in nodes.items() if r["name"] in ENTRY]
ents.sort(key=lambda i:(len(adj.get(i,[])),nodes[i]["line_end"]-nodes[i]["line"]),reverse=True)
entry=ents[0] if ents else None
reach=set()
if entry:
    st=[entry]
    while st:
        x=st.pop()
        if x in reach:continue
        reach.add(x);st.extend(adj.get(x,[]))
outn=[];nosrc=0
for i,r in nodes.items():
    s=sl(r["file"],r["line"],r["line_end"]);nosrc+=(not s)
    outn.append({"id":i,"name":r["name"],"file":r["file"],"line":r["line"],"line_end":r["line_end"],"content":s})
iss=[]
if not entry:iss.append("NO_ENTRY_NODE")
elif not adj.get(entry):iss.append("ENTRY_HAS_NO_CALLEES")
if not edges:iss.append("NO_EDGES")
if entry and len(reach)<=1:iss.append("ENTRY_UNREACHABLE_GRAPH")
if nodes and nosrc/len(nodes)>0.2:iss.append(f"MISSING_SOURCE_{nosrc}/{len(nodes)}")
meta={"challenge":ch,"lang":lang,"entry":entry,"method":"joern-final",
      "n_nodes":len(nodes),"n_edges":len(edges),"edges_resolved":nres,"edges_byname":nname,
      "entry_callees":len(adj.get(entry,[])) if entry else 0,
      "reachable_from_entry":len(reach),"nodes_without_source":nosrc,"sane":not iss,"issues":iss}
json.dump({"meta":meta,"nodes":outn,"edges":edges},open(out,"w"))
print(json.dumps(meta,ensure_ascii=False))

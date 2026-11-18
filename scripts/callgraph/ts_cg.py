# tree-sitter 调用图提取器:逐文件语法解析,出 func + edge jsonl(与 merge.py 同格式)
import sys, os, json
import tree_sitter_c as tsc, tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser
srcroot, out, lang = sys.argv[1], sys.argv[2], sys.argv[3]
LANG = Language(tscpp.language()) if lang=="cpp" else Language(tsc.language())
P = Parser(LANG)
# .inc/.ipp: code #included into a .c/.cpp (capstone's LLVM-generated AArch64GenAsmWriter.inc)
EXTS = (".cc",".cpp",".cxx",".c",".h",".hpp",".hxx",".hh",".inc",".ipp") if lang=="cpp" else (".c",".h",".inc")

def last_name(node, src):
    # 从 declarator / call function 里取最后一个 identifier 片段
    t=node.type
    if t in ("identifier","field_identifier","type_identifier","destructor_name","operator_name"):
        return src[node.start_byte:node.end_byte].decode("utf8","replace")
    if t=="qualified_identifier":
        n=node.child_by_field_name("name")
        return last_name(n,src) if n else None
    if t in ("function_declarator","pointer_declarator","reference_declarator","parenthesized_declarator"):
        d=node.child_by_field_name("declarator")
        return last_name(d,src) if d else None
    if t=="template_function":
        n=node.child_by_field_name("name")
        return last_name(n,src) if n else None
    if t=="field_expression":
        n=node.child_by_field_name("field")
        return last_name(n,src) if n else None
    for c in node.children:
        r=last_name(c,src)
        if r: return r
    return None

def walk_funcs(root, src, rel, pw):
    # 找所有 function_definition,记录名字+行范围,并在其体内找 call_expression
    stack=[root]
    while stack:
        n=stack.pop()
        if n.type=="function_definition":
            decl=n.child_by_field_name("declarator")
            name=last_name(decl,src) if decl else None
            if name:
                ls=n.start_point[0]+1; le=n.end_point[0]+1
                pw.write(json.dumps({"kind":"func","name":name,"file":rel,"line":ls,"line_end":le})+"\n")
                body=n.child_by_field_name("body")
                if body:
                    cs=[body]
                    while cs:
                        m=cs.pop()
                        if m.type=="call_expression":
                            f=m.child_by_field_name("function")
                            callee=last_name(f,src) if f else None
                            if callee:
                                pw.write(json.dumps({"kind":"edge","caller":name,"callee":callee,"file":rel,"line":m.start_point[0]+1})+"\n")
                        elif m.type=="new_expression":
                            t=m.child_by_field_name("type")
                            callee=last_name(t,src) if t else None
                            if callee:
                                pw.write(json.dumps({"kind":"edge","caller":name,"callee":callee,"file":rel,"line":m.start_point[0]+1})+"\n")
                        elif m.type in ("declaration","field_declaration"):
                            t=m.child_by_field_name("type"); callee=last_name(t,src) if t else None
                            # 函数体内 `Type v(...)` 既可能是构造调用(多数)也可能是函数原型;连到 Type
                            if callee and callee[:1].isupper() and any(c.type in ("init_declarator","function_declarator") for c in m.children):
                                pw.write(json.dumps({"kind":"edge","caller":name,"callee":callee,"file":rel,"line":m.start_point[0]+1})+"\n")
                        cs.extend(m.children)
            # 不再深入(嵌套函数罕见)
            continue
        stack.extend(n.children)

nf=0
with open(out,"w") as pw:
    for dp,_,files in os.walk(srcroot):
        for fn in files:
            if not fn.endswith(EXTS): continue
            p=os.path.join(dp,fn); rel=os.path.relpath(p,srcroot)
            try: src=open(p,"rb").read()
            except OSError: continue
            try:
                tree=P.parse(src); walk_funcs(tree.root_node, src, rel, pw); nf+=1
            except Exception: pass
print(f"parsed {nf} files -> {out}")

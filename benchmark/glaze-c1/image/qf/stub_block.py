import re, sys
# Replace the #ifdef FUZZ_TARGET ... #endif block that defines
# LLVMFuzzerTestOneInput with the stub's body (includes dropped).
path, stub = sys.argv[1], open(sys.argv[2]).read()
body = '\n'.join(l for l in stub.splitlines() if not l.startswith('#include'))
src = open(path).read()
out, pos, done = [], 0, False
for m in re.finditer(r'^#ifdef FUZZ_TARGET\n', src, re.M):
  end = src.find('\n#endif', m.end())
  block = src[m.end():end]
  if 'LLVMFuzzerTestOneInput' in block and not done:
    out.append(src[pos:m.end()] + body + '\n')
    pos, done = end, True
out.append(src[pos:])
if not done:
  sys.exit('no FUZZ_TARGET block with LLVMFuzzerTestOneInput')
open(path, 'w').write(''.join(out))

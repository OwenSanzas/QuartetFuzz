#include <fuzzer/FuzzedDataProvider.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <deque>
#include <string>

#include "jq.h"
#include "jv.h"
#include "oniguruma.h"

namespace {

std::string g_module_dir;

jv fuzz_input_cb(jq_state *, void *data) {
  auto *q = static_cast<std::deque<jv> *>(data);
  if (q == nullptr || q->empty())
    return jv_invalid();
  jv v = q->front();
  q->pop_front();
  return v;
}

void quiet_msg_cb(void *, jv msg) {
  jv_free(msg);
}

void timeout_handler(int) {
  _exit(0);
}

const char *kTemplates[] = {
    ".",
    ".a.b[0]?",
    "getpath([0])",
    "setpath([0];1)",
    "delpaths([[0]])",
    "reduce .[] as $x (0; . + $x)",
    "foreach .[] as $x (0; .+$x; .)",
    "try error(\"x\") catch .",
    "label $out | ., break $out",
    "def f(x): x+1; f(.)",
    "if . then 1 elif . == 0 then 2 else 3 end",
    "test(\"a.b\")",
    "match(\"a\";\"g\")",
    "sub(\"a\";\"b\")",
    "scan(\"a\")",
    "capture(\"(?<x>.)\")",
    "strptime(\"%Y-%m-%d\")",
    "gmtime",
    "mktime",
    "strftime(\"%Y\")",
    "sort_by(.)",
    "group_by(.)",
    "unique_by(.)",
    "min_by(.)",
    "max_by(.)",
    "input",
    "inputs",
    "[limit(5; inputs)]",
    "debug",
    "stderr",
    "env",
    "$ENV",
    "halt_error",
    "halt",
    "limit(2;repeat(.))",
    "first(range(10))",
    "tostream",
    "fromstream(tostream)",
    "@base64",
    "@csv",
    "@json",
    "@text",
    "@html",
    "@uri",
    "@sh",
    "ltrimstr(\"a\")",
    "rtrimstr(\"a\")",
    "splits(\",\")",
    "splits(\",\";\"g\")",
    "ascii_downcase",
    "ascii_upcase",
    "$x + $y",
    "$a + $b + $c",
    "$n * $m",
    "path(.a.b)",
    "paths",
    "leaf_paths",
    "to_entries",
    "from_entries",
    "with_entries(.value += 1)",
    "walk(if type == \"number\" then . + 1 else . end)",
    "implode",
    "explode",
    "recurse",
    "recurse(.[]?)",
    "..",
    "range(0;10;2)",
    "while(. < 10; . + 1)",
    "until(. >= 10; . + 1)",
    "repeat(.) | limit(3;.)",
    "input_line_number",
    "now | todate",
    "min, max",
    "add",
    "flatten",
    "transpose",
    "combinations",
    "ltrimstr(\"\") // empty",
    ". as [$a,$b] ?// $a | $a",
    "try (1/0) catch .",
    "def inc: . + 1; inc",
    "any", "all",
    "unique",
    "indices(\"a\")",
    "index(\"a\")",
    "rindex(\"a\")",
    "splits(\" \")",
    "ltrimstr(\"a\") | rtrimstr(\"b\")",
    "bsearch(0)",
    "getpath([\"a\",0])",
    "[.[] | select(. > 0)]",
    "try error catch .",
    "import \"nonexistent\" as foo; .",

    ".a |= . + 1",
    ".a += 1",
    ".a -= 1",
    ".a *= 2",
    ".a /= 2",
    ".a %= 2",
    ".a //= 1",
    "del(.a)",
    "del(.a, .b)",
    "(.a,.b) |= .+1",
    ".a.b.c = 5",
    "path(.a.b) = [1,2]",

    "try test(\"(\") catch .",
    "try match(\"[\") catch .",
    "try splits(\"(\") catch .",
    "try capture(\"(?<\") catch .",

    "{(.a): .b}",
    "{(\"x\"): 1, (\"y\"): 2}",
    "[.[], .[]]",
    "{a: (1,2), b: (3,4)}",

    "label $a | label $b | ., break $a",
    "label $a | (label $b | break $a), 1",

    "nan",
    "infinite",
    "-infinite",
    "isnan",
    "isinfinite",
    "isnormal",

    "try ltrimstr(\"a\") catch .",
    "try explode catch .",
    "try implode catch .",

    "input_filename",
    "$__prog_name",
    "localtime",
    "now | localtime | strftime(\"%Y\")",
    "splits(\",\";\"x\")",
    "try getpath([0]) catch .",
    "try (1 | getpath([\"a\"])) catch .",
    "ltrimstr(\"a\") == .",
    "rtrimstr(\"c\") == .",
    "test(\"a,b,c\")",
    "capture(\"(?<x>a),(?<y>b)\")",
    "[splits(\",\")]",
    "getpath([]) ",
    "recurse(if type==\"array\" then .[] else empty end)",
    "walk(.)",

    "strflocaltime(\"%Y-%m-%d\")",
    "have_decnum",
    "have_literal_numbers",
    "trim",
    "ltrim",
    "rtrim",
    "try trim catch .",
    "utf8bytelength",
    "pick(.a,.b)",
    "in([1,2,3])",
    "inside([1,2,3])",
    "IN(1,2,3)",
    "INDEX(.id?)",
    "JOIN({\"a\":1}; .a)",
    "nth(0; range(5))",
    "nth(0)",
    "skip(1; .[])",
    "isempty(empty)",
    "abs",
    "reverse",
    "join(\",\")",
    "@tsv",
    "gsub(\"a\";\"b\")",
    "truncate_stream(1|tostream)",

    "import \"fuzzmod\" as m; m::f",
    "import \"fuzzmod\" as m; m::g(1)",
    "import \"fuzzmod\" as m; m::val",
    "import \"fuzzmod\" as m; m::h",
    "import \"fuzzmod\" as m; m::f + 1",
    "\"fuzzmod\" | modulemeta",

    "try (1|startswith(\"a\")) catch .",
    "try ({}|splits(\",\")) catch .",
    "try (1|setpath([\"a\"];1)) catch .",
    "try (\"x\"|has(0)) catch .",
    "try (1|has(\"x\")) catch .",
    "try (1|contains(\"a\")) catch .",
    "try (\"x\"|contains(1)) catch .",
    "try (null|keys) catch .",
    "try (1|to_entries) catch .",
    "try (1|explode) catch .",

    "try (reduce .[] as $x (0; if $x==0 then error(\"z\") else .+$x end)) catch .",
    "label $out | foreach .[] as $x (0; .+$x; if . > 5 then ., break $out else empty end)",
    ". as [$a,$b] ?// {$a,$b} | try ($a+$b) catch .",
    "label $a | reduce .[] as $x (0; if $x > 3 then break $a else .+$x end)",
    "try (label $a | foreach .[] as $x (0; .+$x; if . > 100 then error(\"big\") else . end)) catch .",

    "toboolean",
    "try toboolean catch .",
    "endswith(\"a\")",
    "try (1|endswith(\"a\")) catch .",
    "keys_unsorted",
    "try (1|keys_unsorted) catch .",
    "get_search_list",
    "get_prog_origin",
    "get_jq_origin",
    "split(\",\")",
    "try (1|split(\",\")) catch .",
    "length",

    "@base64d",
    "\"abc\" | @base64 | @base64d",
    "try (\"not base64!!!\" | @base64d) catch .",

    "test(\"A\";\"i\")",
    "match(\"a\";\"ix\")",
    "scan(\"a\";\"i\")",
    "gsub(\"a\";\"b\";\"i\")",
    "sub(\"(?<x>a)\";\"[\\(.x)]\";\"g\")",
    "try test(\"a\";\"badflag\") catch .",
};

const char *kFragments[] = {
    ".", "1+1", ".a", ".[]?", "select(.>0)", "map(.+1)", "to_entries",
    "reduce .[] as $x (0; .+$x)", "foreach .[] as $x (0;.+$x;.)",
    "try error catch .", "label $L | ., break $L",
    "sort_by(.)", "group_by(.)", "unique", "paths", "walk(.)",
    "gsub(\"a\";\"b\")", "splits(\",\")", "pick(.a)", "in([0,1,2])",
    "inside([0,1,2])", "INDEX(.)", "IN(1,2,3)", "nth(0)?", "skip(1;.[])",
    "isempty(empty)", "abs", "reverse", "join(\",\")", "trim", "ltrim",
    "rtrim", "@tsv", "strflocaltime(\"%Y\")?", "have_decnum",
    "have_literal_numbers", "modulemeta?", "$__prog_name", "utf8bytelength?",
    "ascii_downcase?", "ascii_upcase?", "flatten", "transpose?",
    "combinations", "getpath([0])", "setpath([0];1)", "delpaths([[0]])",
    "del(.a)", "ltrimstr(\"a\")?", "rtrimstr(\"a\")?", "explode?", "implode?",
    "tostream", "fromstream(tostream)", "env", "now", "input_line_number",
    "test(\"a\")?", "capture(\"(?<x>.)\")?",

    "toboolean?", "endswith(\"a\")?", "keys_unsorted", "split(\",\")?",
    "@base64d?", "test(\"a\";\"i\")?", "length",
};

jv MakeFuzzedJv(FuzzedDataProvider &fdp, int depth = 0) {
  int max_kind = depth > 3 ? 4 : 7;
  int kind = fdp.ConsumeIntegralInRange<int>(0, max_kind);
  switch (kind) {
    case 0:
      return jv_null();
    case 1:
      return jv_bool(fdp.ConsumeBool());
    case 2: {

      if (fdp.ConsumeBool()) {
        std::string lit;
        if (fdp.ConsumeBool()) lit += "-";
        int ndigits = fdp.ConsumeIntegralInRange<int>(1, 30);
        for (int i = 0; i < ndigits; i++)
          lit += static_cast<char>('0' + fdp.ConsumeIntegralInRange<int>(0, 9));
        if (fdp.ConsumeBool()) {
          lit += ".";
          int nfrac = fdp.ConsumeIntegralInRange<int>(0, 10);
          for (int i = 0; i < nfrac; i++)
            lit += static_cast<char>('0' + fdp.ConsumeIntegralInRange<int>(0, 9));
        }
        if (fdp.ConsumeBool()) {
          lit += "e";
          if (fdp.ConsumeBool()) lit += "-";
          lit += std::to_string(fdp.ConsumeIntegralInRange<int>(0, 400));
        }
        jv v = jv_parse(lit.c_str());
        if (jv_is_valid(v)) return v;
        jv_free(v);
      }
      return jv_number(fdp.ConsumeIntegralInRange<int>(-100000, 100000));
    }
    case 3:
      return jv_number(fdp.ConsumeFloatingPoint<double>());
    case 4: {
      std::string s = fdp.ConsumeRandomLengthString(16);
      if (fdp.ConsumeBool()) {
        static const char *kNeedles[] = {"a", "a.b", ",", "a,b,c", "aXbXc",
                                          "c", "abc"};
        s += kNeedles[fdp.ConsumeIntegralInRange<size_t>(
            0, sizeof(kNeedles) / sizeof(kNeedles[0]) - 1)];
      }
      return jv_string_sized(s.data(), s.size());
    }
    case 5: {
      jv arr = jv_array();
      int n = fdp.ConsumeIntegralInRange<int>(0, 4);
      for (int i = 0; i < n; i++)
        arr = jv_array_append(arr, MakeFuzzedJv(fdp, depth + 1));
      return arr;
    }
    case 6: {
      jv obj = jv_object();
      int n = fdp.ConsumeIntegralInRange<int>(0, 4);
      for (int i = 0; i < n; i++) {
        std::string k = fdp.ConsumeRandomLengthString(8);
        if (k.empty()) k = "k";
        obj = jv_object_set(obj, jv_string(k.c_str()),
                             MakeFuzzedJv(fdp, depth + 1));
      }
      return obj;
    }
    default:
      return jv_number(0);
  }
}

std::string BuildComposedProgram(FuzzedDataProvider &fdp) {
  constexpr size_t kFragCount = sizeof(kFragments) / sizeof(kFragments[0]);
  int n = fdp.ConsumeIntegralInRange<int>(1, 4);
  std::string prog;
  for (int i = 0; i < n; i++) {
    size_t idx = fdp.ConsumeIntegralInRange<size_t>(0, kFragCount - 1);
    if (i > 0) {
      int sep = fdp.ConsumeIntegralInRange<int>(0, 1);
      prog += (sep == 0) ? " | " : ", ";
    }
    prog += kFragments[idx];
  }

  int wrap = fdp.ConsumeIntegralInRange<int>(0, 4);
  switch (wrap) {
    case 1:
      prog = "try (" + prog + ") catch .";
      break;
    case 2:
      prog = "label $L | (" + prog + "), break $L";
      break;
    case 3:
      prog = "reduce (" + prog + ") as $x (0; .+1)";
      break;
    case 4:
      prog = "[" + prog + "]";
      break;
    default:
      break;
  }
  return prog;
}

}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {

  struct sigaction sa;
  sa.sa_handler = timeout_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGALRM, &sa, nullptr);

  onig_set_parse_depth_limit(1024);

  char dir_template[] = "/tmp/jq_fuzz_mod_XXXXXX";
  char *dir = mkdtemp(dir_template);
  if (dir != nullptr) {
    g_module_dir = dir;
    std::string mod_path = g_module_dir + "/fuzzmod.jq";
    FILE *f = fopen(mod_path.c_str(), "w");
    if (f != nullptr) {
      fputs(
          "def f: 1;\n"
          "def g(x): x + 1;\n"
          "def val: 42;\n"
          "def h: modulemeta;\n",
          f);
      fclose(f);
    } else {
      g_module_dir.clear();
    }
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  int program_choice = fdp.ConsumeIntegralInRange<int>(0, 99);
  std::string prog_payload;
  if (program_choice < 5) {
    prog_payload = fdp.ConsumeRandomLengthString();
  } else if (program_choice < 55) {
    prog_payload = BuildComposedProgram(fdp);
  } else {
    size_t idx = fdp.ConsumeIntegralInRange<size_t>(
        0, sizeof(kTemplates) / sizeof(kTemplates[0]) - 1);
    prog_payload = kTemplates[idx];
  }

  int x = fdp.ConsumeIntegralInRange<int>(-1000, 1000);
  int y = fdp.ConsumeIntegralInRange<int>(-1000, 1000);
  int a = fdp.ConsumeIntegralInRange<int>(-1000, 1000);
  int b = fdp.ConsumeIntegralInRange<int>(-1000, 1000);
  int c = fdp.ConsumeIntegralInRange<int>(-1000, 1000);
  int n = fdp.ConsumeIntegralInRange<int>(-1000, 1000);
  int m = fdp.ConsumeIntegralInRange<int>(-1000, 1000);

  jv args = jv_object();
  args = jv_object_set(args, jv_string("x"), jv_number(x));
  args = jv_object_set(args, jv_string("y"), jv_number(y));
  args = jv_object_set(args, jv_string("a"), jv_number(a));
  args = jv_object_set(args, jv_string("b"), jv_number(b));
  args = jv_object_set(args, jv_string("c"), jv_number(c));
  args = jv_object_set(args, jv_string("n"), jv_number(n));
  args = jv_object_set(args, jv_string("m"), jv_number(m));

  bool use_structured_input = fdp.ConsumeIntegralInRange<int>(0, 9) < 8;

  jv input;
  std::string input_payload;
  if (use_structured_input) {
    input = MakeFuzzedJv(fdp);
  } else {
    input_payload = fdp.ConsumeRandomLengthString();
    input = jv_parse(input_payload.c_str());
  }

  std::deque<jv> pending;
  {
    int queue_mode = fdp.ConsumeIntegralInRange<int>(0, 9);
    if (queue_mode < 8) {
      int count = fdp.ConsumeIntegralInRange<int>(0, 6);
      for (int i = 0; i < count; i++) {
        pending.push_back(MakeFuzzedJv(fdp));
      }
    } else {
      std::string extra_input = fdp.ConsumeRemainingBytesAsString();
      size_t start = 0;
      while (start <= extra_input.size()) {
        size_t nl = extra_input.find('\n', start);
        std::string line = (nl == std::string::npos)
                                ? extra_input.substr(start)
                                : extra_input.substr(start, nl - start);
        if (!line.empty()) {
          jv v = jv_parse(line.c_str());
          if (jv_is_valid(v)) {
            pending.push_back(v);
          } else {
            jv_free(v);
          }
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
    }
  }

  jq_state *jq = jq_init();
  if (jq == NULL) {
    jv_free(args);
    jv_free(input);
    while (!pending.empty()) {
      jv_free(pending.front());
      pending.pop_front();
    }
    return 0;
  }

  jq_set_error_cb(jq, quiet_msg_cb, nullptr);
  jq_set_debug_cb(jq, quiet_msg_cb, nullptr);
  jq_set_stderr_cb(jq, quiet_msg_cb, nullptr);

  jq_set_attr(jq, jv_string("JQ_ORIGIN"), jv_string("/tmp/"));

  if (!g_module_dir.empty()) {
    jq_set_attr(jq, jv_string("JQ_LIBRARY_PATH"),
                JV_ARRAY(jv_string(g_module_dir.c_str())));
  }

  jq_set_input_cb(jq, fuzz_input_cb, &pending);

  if (jq_compile_args(jq, prog_payload.c_str(), args)) {
    if (jv_is_valid(input)) {

      alarm(5);

      jq_start(jq, input, 0);

      const int kMaxResults = 1000;
      for (int i = 0; i < kMaxResults; ++i) {
        jv result = jq_next(jq);
        bool valid = jv_is_valid(result);
        jv_free(result);
        if (!valid) {
          break;
        }
      }

      alarm(0);
    } else {

      jv_free(input);
    }
  } else {

    jv_free(input);
  }

  jq_teardown(&jq);

  while (!pending.empty()) {
    jv_free(pending.front());
    pending.pop_front();
  }

  return 0;
}
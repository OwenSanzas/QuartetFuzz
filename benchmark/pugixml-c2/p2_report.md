## target: pugi::xpath_query::xpath_query(const char_t* query, xpath_variable_set* variables = NULL)

### P2.1 Init Sequence
**Claim:** No global library init is required. The only thing that must exist *before* calling the target is, optionally, a fully-populated `xpath_variable_set` (built via `add`/`set` calls) if the query text references `$name` variables. No XML document is required to construct the query (parsing is purely textual); a document/`xpath_node` is only needed later for `evaluate_*`.
**Evidence:**
> src/pugixml.hpp:1327-1329: "// Construct a compiled object from XPath expression. ... explicit xpath_query(const char_t* query, xpath_variable_set* variables = PUGIXML_NULL);"
> src/pugixml.cpp:13427-13444: constructor body allocates `xpath_query_impl` then immediately calls `impl::xpath_parser::parse(query, variables, &qimpl->alloc, &_result)` — no other state is touched.
> tests/test_xpath_variables.cpp:215-221,229: `xpath_variable_set set; set.set(STR("var"), ...); ... xpath_query q(STR("$var"), &set);` — variables are configured before constructing the query.

### P2.2 Parameter Construction
**Claim:** `query` is a `const char_t*` (NUL-terminated, narrow `char` unless `PUGIXML_WCHAR_MODE` is defined) raw XPath text — any byte sequence is accepted by the lexer and either parses or fails with a parse error; no length parameter. `variables` is an optional pointer to a caller-owned `xpath_variable_set` (may be `NULL` for "no variables"); the parser only reads from it (via `_variables` member) during `parse_variable_ref`, it does not take ownership.
**Evidence:**
> src/pugixml.hpp:1329: `explicit xpath_query(const char_t* query, xpath_variable_set* variables = PUGIXML_NULL);`
> src/pugixml.cpp:12830: `xpath_parser(const char_t* query, xpath_variable_set* variables, xpath_allocator* alloc, xpath_parse_result* result): _alloc(alloc), _lexer(query), _query(query), _variables(variables), _result(result), _depth(0)` — `query` must be non-NULL (passed straight to `xpath_lexer`); `variables` may be NULL.
> src/pugixml.cpp:9487: `get_variable_scratch(..., xpath_variable_set* set, ...)` — used when resolving `$name` references; if `set` is NULL, lookups simply fail (reported as parse errors), it is not dereferenced unconditionally.

### P2.3 Object Lifecycle
**Claim:** Create (via the 2-arg ctor, or default ctor for an empty/invalid query) → evaluate one or more times with `evaluate_*` → destroy via `~xpath_query()` (or implicit scope exit / move-out). `xpath_query` is non-copyable (copy ctor/assignment are private/deleted) but move-constructible/assignable when `PUGIXML_HAS_MOVE` is defined.
**Evidence:**
> src/pugixml.hpp:1322-1324: "// Non-copyable semantics\n\txpath_query(const xpath_query&);\n\txpath_query& operator=(const xpath_query&);" (private, unimplemented)
> src/pugixml.hpp:1337-1341: move ctor/assign declared under `PUGIXML_HAS_MOVE`.
> src/pugixml.cpp:13469-13473: `~xpath_query() { if (_impl) impl::xpath_query_impl::destroy(...); }` — destructor frees the compiled AST + allocator pages.
> src/pugixml.cpp:13476-13497: move ctor/operator= steal `_impl`/`_result` and null out the source, consistent with a single owner at a time.

### P2.4 Return Value Handling
**Claim:** The constructor has no return value; success/failure is signalled two ways depending on build config. With exceptions enabled (default, `PUGIXML_NO_EXCEPTIONS` not defined) a parse failure throws `pugi::xpath_exception` (constructed from the `xpath_parse_result`), and an allocation failure throws `std::bad_alloc`. With `PUGIXML_NO_EXCEPTIONS` defined, no exception is thrown; instead `_result.error` is set and the object's `_impl` stays `NULL`, so the caller must check `result()` or the safe-bool operator (`operator unspecified_bool_type()` / `operator!()`) before using the query.
**Evidence:**
> src/pugixml.cpp:13429-13461: on `!qimpl` → throws `std::bad_alloc()` (or sets `_result.error = "Out of memory"`); on `qimpl->root == NULL` → throws `xpath_exception(_result)` (or sets error string) depending on `PUGIXML_NO_EXCEPTIONS`.
> src/pugixml.hpp:1377-1384: `const xpath_parse_result& result() const; operator unspecified_bool_type() const; bool operator!() const;` — the documented way to check validity after construction.
> ossfuzz build: `tests/fuzz_parse.cpp` / project config does **not** define `PUGIXML_NO_EXCEPTIONS` (src/pugiconfig.hpp:31 shows it commented out), so a fuzz harness constructing `xpath_query` on malformed input must be prepared to catch `pugi::xpath_exception` / `std::bad_alloc`, or it will abort/crash the process (this is expected library behavior, not a bug, but the harness must handle it, e.g. with try/catch, to keep fuzzing alive).

### P2.5 Cleanup Sequence
**Claim:** Only the `xpath_query` object itself needs destruction (its destructor releases the internal `xpath_query_impl`, which releases the whole `xpath_allocator` memory-block chain in one call). If a caller-supplied `xpath_variable_set` was passed in, it is owned by the caller and must be destroyed separately (and must outlive both construction and every `evaluate_*` call, since `ast_variable` nodes keep a raw pointer `_data.variable` into it).
**Evidence:**
> src/pugixml.cpp:13469-13473: `~xpath_query()` is the only required cleanup call.
> src/pugixml.cpp:12866-12873: `xpath_query_impl::destroy` does `impl->alloc.release();` then `xml_memory::deallocate(impl);` — single place that frees all AST/allocator memory.
> src/pugixml.cpp:12245: `return alloc_node(ast_variable, var->type(), var);` — the AST stores a raw pointer to the variable inside the caller's `xpath_variable_set`; destroying the `xpath_variable_set` before evaluating/destroying the query is a use-after-free.
> tests/test_xpath_variables.cpp:215-221: variable set `set` is declared before, and kept alive across, `xpath_query q(... , &set)` and the following `evaluate_*` calls.

### P2.6 API Existence
**Claim:** All functions named in this report exist in the current codebase (verified by grep/read of `pugixml.hpp`/`pugixml.cpp`).
**Evidence:**
> src/pugixml.hpp:1329,1332,1335,1344,1348,1352,1357,1364,1369,1375,1378,1381,1384 — xpath_query member declarations.
> src/pugixml.hpp:1300,1303-1306,1309-1310 — xpath_variable_set::add/set/get declarations.
> src/pugixml.hpp:766-775 — xml_node::select_node/select_nodes/select_single_node declarations.
> src/pugixml.cpp:13427,13465,13469,13500,13507,13528,13550,13572,13602,13624,13655,13660 — xpath_query member definitions.
> src/pugixml.cpp:13367,13393-13425 — xpath_variable_set::add/set/get definitions.
> src/pugixml.cpp:13665-13696 — xml_node::select_node/select_nodes/select_single_node definitions.
> src/pugixml.cpp:12848-12853 — impl::xpath_parser::parse (static).
> src/pugixml.cpp:12856-12885 — impl::xpath_query_impl (create/destroy).

### P2.7 Co-call Constraints
**Claim:** (1) Constructor/destructor pair: every successfully- or partially-constructed `xpath_query` must eventually be destroyed exactly once (RAII; no explicit "close" call). (2) `xpath_variable_set::add` must be called (to register a name+type) before `xpath_variable_set::set`/`get` can succeed for that name — `set()` internally calls `add()` first. (3) `xpath_query(query, variables)` and every subsequent `evaluate_*` call on it, plus the `xpath_variable_set*` passed to the constructor, must all be used with the same variable set alive — there is no "rebind" API. (4) `select_node`/`select_nodes`/`select_single_node(const char_t*, variables)` internally construct a temporary `xpath_query`, evaluate it once, and destroy it — they are self-contained convenience wrappers, not something you pair manually with `xpath_query`'s ctor/dtor.
**Evidence:**
> src/pugixml.cpp:13393-13396: `xpath_variable_set::set(name, bool value) { xpath_variable* var = add(name, xpath_type_boolean); return var ? var->set(value) : false; }` — set() calls add() internally, demonstrating add-before-use pattern even inside the library.
> src/pugixml.cpp:13665-13669: `xml_node::select_node(query, variables) { xpath_query q(query, variables); return q.evaluate_node(*this); }` — ctor+evaluate+implicit dtor (temporary) bundled.
> tests/test_xpath_api.cpp:16-19: `doc.select_nodes(STR("node/foo"))` vs. explicit `xpath_query q(STR("node/foo")); doc.select_nodes(q);` show the two equivalent call protocols.

### P2.8 Prerequisite State
**Claim:** No XML document, no global library state, and no prior `xpath_query` are prerequisites for constructing the target. The only prerequisite state is an already-populated `xpath_variable_set` *if* the query text contains `$name` references (otherwise parsing a `$name` reference against a NULL/variable-less set is a parse error, not a crash). For evaluation afterwards (`evaluate_*`), a valid `xpath_node` context (often obtained from an `xml_document`/`xml_node`) is required, but that is a prerequisite of the *consumer* calls, not of the constructor itself.
**Evidence:**
> src/pugixml.cpp:13427-13444: constructor only touches `query`, `variables`, and freshly allocated `qimpl`.
> src/pugixml.hpp:1416-1420 (xpath_node) and src/pugixml.cpp:13507-13511 (`evaluate_boolean` builds `impl::xpath_context c(n, 1, 1)` from the passed `xpath_node n`) — evaluate_* needs a context node, not the constructor.

### P2.9 Result Usage
**Claim:** The constructed `xpath_query` object (its internal `_impl`/AST) is consumed by the six `evaluate_*` methods and by `return_type()`; all of them `static_cast<impl::xpath_query_impl*>(_impl)` and walk `->root->eval_*`. `xml_node::select_node(const xpath_query&)`, `select_nodes(const xpath_query&)` and `select_single_node(const xpath_query&)` also take an already-built `xpath_query` and call its `evaluate_node`/`evaluate_node_set` on `*this` xml_node. Validity (`_impl != NULL`) must be checked (via `operator bool`/`operator!`) before relying on evaluation results in `PUGIXML_NO_EXCEPTIONS` builds, since all `evaluate_*` methods guard `if (!_impl) return <default>;`.
**Evidence:**
> src/pugixml.cpp:13507-13509: `evaluate_boolean` — `if (!_impl) return false;` then uses `_impl`.
> src/pugixml.cpp:13602-13621: `evaluate_node_set` calls `impl::evaluate_node_set_prepare(_impl)` which checks `impl->root->rettype() != xpath_type_node_set` (type mismatch handling) at src/pugixml.cpp:12887-12899, then `root->eval_node_set(c, sd.stack, impl::nodeset_eval_all)`.
> src/pugixml.cpp:13671-13696: `xml_node::select_node(const xpath_query& query) const { return query.evaluate_node(*this); }` and analogous `select_nodes`/`select_single_node` overloads — real caller pattern of reusing one compiled `xpath_query` against (potentially) multiple `xml_node` contexts.
> tests/test_xpath_api.cpp:18-19,29-30: `xpath_query q(STR("node/foo")); xpath_node_set ns2 = doc.select_nodes(q);` and `xpath_node n2 = doc.select_node(q);` — direct evidence of result (the `xpath_query`) being fed into `select_node`/`select_nodes`.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting inputs are: (a) the `query` string itself — operators, axes, node tests, predicates, function calls select different grammar productions inside `xpath_parser`; (b) presence/absence and *type* (`xpath_type_boolean/number/string/node_set`) of variables in `variables`, which changes whether `parse_variable_ref`/`ast_variable` nodes are created and which `xpath_variable::get_*`/`set` branch is taken; (c) which `evaluate_*` method the caller invokes afterwards selects `eval_boolean`/`eval_number`/`eval_string`/`eval_node_set` and the implicit type-coercion rules; (d) a compile-time `PUGIXML_XPATH_DEPTH_LIMIT` macro (defaults to a fixed constant) bounds the recursive-descent parser's `_depth` counter, turning deeply nested expressions into a parse error (`error_rec()`) instead of unbounded recursion; (e) `PUGIXML_NO_EXCEPTIONS` toggles error reporting from C++ exceptions to `_result.error`/sentinel return values.
**Evidence:**
> src/pugixml.cpp:11901-11905: `static const size_t xpath_ast_depth_limit = #ifdef PUGIXML_XPATH_DEPTH_LIMIT PUGIXML_XPATH_DEPTH_LIMIT #else ... #endif` — recursion guard constant.
> src/pugixml.cpp:12310-12311,12349-12350,12528-12529,12573-12574,12767-12768,12817-12818: numerous `if (++_depth > xpath_ast_depth_limit) return error_rec();` checks scattered across predicate/path/union/relational/additive parsing — every recursive grammar rule is depth-guarded.
> src/pugixml.cpp:9409-9422: `new_xpath_variable(type, name)` switches on `xpath_type_node_set/string/boolean/number` to allocate the matching `xpath_variable_*` subtype.
> src/pugixml.cpp:13507,13528,13550,13572,13602,13624: the six distinct `evaluate_*` entry points each call a different `eval_*` on the same AST root.
> src/pugixml.cpp:13433-13437,13455-13460: `#ifdef PUGIXML_NO_EXCEPTIONS` branches in the constructor change error signalling.

### P2.11 Input Expectations
**Claim:** `query` must be a NUL-terminated `char_t*` buffer (no explicit length parameter — the lexer scans until `\0`); there is no documented maximum length other than the depth limit on *nested* constructs (flat long strings/number literals are only bounded by available memory via the bump allocator). The grammar is XPath 1.0; encoding is whatever `char_t` is configured as (`char` by default, `wchar_t` under `PUGIXML_WCHAR_MODE`) — the lexer itself does not re-validate UTF‑8/wide-char well-formedness, it just scans ASCII-ish XPath tokens plus escaped string literals. Malformed input (syntax errors, unknown function/axis names, unterminated string literal, depth-limit exceeded) is reported via `xpath_parse_result`/`xpath_exception`, not via corrupting the returned object; the constructor leaves `_impl == NULL` in the failure case before exceptions (if disabled) so the object behaves as an "empty" query (`operator bool` false).
**Evidence:**
> src/pugixml.hpp:1329: `explicit xpath_query(const char_t* query, ...)` — single pointer parameter, no length; `char_t` typedef (src/pugixml.hpp, PUGIXML_WCHAR_MODE-dependent) defines the character type.
> src/pugixml.cpp:12830: `xpath_lexer _lexer(query)` constructed straight from the raw pointer — relies on NUL termination (lexer internals scan forward until `\0`/unexpected char).
> src/pugixml.cpp:13444-13461: parse failure (`qimpl->root == NULL`) is the sole error path; `_impl` is left `NULL` unless parse succeeded.
> src/pugixml.hpp:1217-1221 (`struct xpath_parse_result { const char* error; ptrdiff_t offset; ... }`) — error reporting carries offset into `query` where parsing stopped, confirming the parser tracks a position into the attacker string rather than any separate length argument.

## Other entries of the Logic Group (how they combine with the target in one harness)

### pugi::xpath_query::evaluate_boolean / evaluate_number / evaluate_string / evaluate_node_set / evaluate_node
**Claim:** All require a successfully constructed `xpath_query` (`_impl != NULL` ideally, though every method null-checks and degrades gracefully to a default value when `_impl==NULL`) and an `xpath_node` context argument (can be `pugi::xpath_node()` default-constructed "null" node, or a node obtained from a parsed `xml_document`). They do not need a prior `select_node`/`select_nodes` call; they are alternatives to those. `evaluate_node_set`/`evaluate_node` additionally require the query's `return_type()` to be `xpath_type_node_set`, otherwise they return an empty result (no exception at that stage) via `impl::evaluate_node_set_prepare`. They can throw `std::bad_alloc` on allocator OOM (`sd.oom`) when exceptions are enabled.
**Evidence:**
> src/pugixml.cpp:13507-13526 (evaluate_boolean), 13528-13547 (evaluate_number), 13550-13569 (evaluate_string, STL variant), 13572-13600 (evaluate_string buffer variant), 13602-13622 (evaluate_node_set), 13624- (evaluate_node): all construct `impl::xpath_context c(n, 1, 1); impl::xpath_stack_data sd;` then call the matching `eval_*` on `root`, then check `sd.oom`.
> src/pugixml.cpp:12887-12899: `evaluate_node_set_prepare` returns `NULL` when `impl->root->rettype() != xpath_type_node_set` (after converting via `ast_filter`/error, see surrounding code) — guards type mismatch for node-set evaluators.
> Harness combination: construct target `xpath_query q(query_str, variables_ptr_or_null)`, wrap in try/catch(xpath_exception/bad_alloc) to survive malformed input, then on success call one or more of `q.evaluate_boolean(ctx)`, `q.evaluate_number(ctx)`, `q.evaluate_string(ctx)`, `q.evaluate_node_set(ctx)`, `q.evaluate_node(ctx)` with `ctx` either `xpath_node()` or a node from a separately-parsed `xml_document` (see tests/test_xpath_api.cpp:14-23 pattern).

### pugi::xml_node::select_node / pugi::xml_node::select_nodes
**Claim:** Convenience wrappers that each internally perform "construct xpath_query from (query,variables) → evaluate_node/evaluate_node_set(*this) → implicitly destroy the temporary `xpath_query`" in one call. Require `*this` to be a valid `xml_node` (typically from a successfully-loaded `xml_document`, though pugixml allows calling on any node including the document node itself). Also overloaded to accept an already-built `xpath_query&` (reuse pattern for repeated evaluation of the same compiled query against different nodes/documents), in which case the caller is responsible for the `xpath_query`'s lifetime (construct before, destroy after use — see P2.3/P2.7).
**Evidence:**
> src/pugixml.hpp:766-771 (declarations), src/pugixml.cpp:13665-13685 (definitions) as quoted above (P2.7/P2.9).
> tests/test_xpath_api.cpp:14-23: both the string-based call (`doc.select_nodes(STR("node/foo"))`) and the pre-built-query call (`xpath_query q(...); doc.select_nodes(q);`) are exercised side by side and checked for equal results.
**Combine in one harness:** Either (a) call `xml_node::select_node/select_nodes(query_str, variables)` directly (this exercises the target implicitly, hidden inside the wrapper, and is a legitimate but *indirect* way to reach the target — the Logic Group's listed "target" ctor is still invoked internally), or (b) explicitly construct `xpath_query q(query_str, variables)` first (the actual target call site that must be instrumented/observed) and then pass `q` into `select_node(q)`/`select_nodes(q)`, letting the harness also cover the `const xpath_query&` overloads and the multi-evaluation reuse pattern.

### pugi::xpath_variable_set::add / pugi::xpath_variable_set::set
**Claim:** `add(name, type)` creates (or returns existing, if types match) a named variable of a given `xpath_value_type` (`xpath_type_boolean/number/string/node_set`; passing `xpath_type_none` or `xpath_type_any` is explicitly rejected returning `NULL`, confirmed by `tests/test_xpath_variables.cpp:9-15` for `xpath_type_none`). Must be called (directly, or indirectly through `set`) *before* constructing an `xpath_query` that references `$name`, and the `xpath_variable_set` object must stay alive across the `xpath_query` constructor call and every subsequent `evaluate_*` call (P2.5). `set(name, value)` is a convenience that calls `add(name, matching_type)` then `var->set(value)`; returns `false` (no var created/mismatched type) without crashing.
**Evidence:**
> src/pugixml.cpp:13367-13391 (`add`), 13393-13415 (`set` overloads for bool/double/const char_t*/xpath_node_set).
> src/pugixml.hpp:1300,1303-1306 (declarations).
> tests/test_xpath_variables.cpp:9-15 (`xpath_type_none` rejected), 215-221 (typical add/set-then-construct-query-then-evaluate sequence), 224-244 (type-mismatch between variable's bound type and query's use of it is reported via `xpath_exception`/empty result at evaluate time, not at construct/add time).
**Combine in one harness:** `xpath_variable_set vs; vs.add(name, type)` (or `vs.set(name, value)` which implies `add`) for each variable the fuzzed query text might reference by `$name`, done *before* `xpath_query q(query_str, &vs)`, and `vs` must outlive `q` and all its `evaluate_*` calls.

## Build notes
**Claim:** The project is built as C++ (pugixml is C++-only); `src/pugixml.cpp` (which `#include`s `pugixml.hpp`) is compiled once into an object file and linked into each libFuzzer harness binary alongside `$LIB_FUZZING_ENGINE`. A new/edited fuzz harness for the XPath feature should `#include "pugixml.hpp"` using the same relative include path as the existing `tests/fuzz_parse.cpp`/`tests/fuzz_xpath.cpp` (`"../src/pugixml.hpp"` from `tests/`), and does not need extra `-l` libraries beyond libstdc++ (`xpath_exception` derives from `std::exception`) since pugixml has no external dependencies.
**Evidence:**
> ossfuzz_view/pugixml-c2/build.sh:17-25: `cd pugixml` then `$CXX $CXXFLAGS -c src/pugixml.cpp -o src/pugixml.o` followed by `$CXX $CXXFLAGS $LIB_FUZZING_ENGINE tests/fuzz_parse.cpp src/pugixml.o -o ${OUT}/fuzz_parse` and the analogous `fuzz_xpath.cpp` build line — shows the single-TU-compile + link-with-harness pattern.
> ossfuzz_view/pugixml-c2/build.sh:19-21: `CXXFLAGS="${CXXFLAGS} -fno-sanitize=float-divide-by-zero"` — XPath evaluation (numeric `div`/`mod`) intentionally triggers float-divide-by-zero, explicitly suppressed for this project's fuzzers (OSS-Fuzz issue #10564 referenced in the comment) — relevant since the target's query language includes `div`/`mod` operators (src/pugixml.cpp arithmetic eval paths).
> ossfuzz_view/pugixml-c2/Dockerfile:16-20: `FROM gcr.io/oss-fuzz-base/base-builder` then `git clone --depth=1 https://github.com/zeux/pugixml` — standard OSS-Fuzz C++ build image, no extra XPath-specific dependencies.
> src/pugixml-c2/pugixml/tests/fuzz_parse.cpp:1: `#include "../src/pugixml.hpp"` — the include path pattern a new `tests/fuzz_xpath_query.cpp`-style harness should follow.
> src/pugixml-c2/pugixml/src/pugiconfig.hpp:31: `// #define PUGIXML_NO_EXCEPTIONS` (commented out/disabled) — confirms exceptions are enabled in this build, so the harness must handle `pugi::xpath_exception`/`std::bad_alloc` (P2.4) rather than relying on sentinel-value error reporting.
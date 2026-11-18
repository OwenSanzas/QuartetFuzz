## target: boost::program_options::parse_config_file

### P2.1 Init Sequence
**Claim:** Before calling `parse_config_file(istream&, const options_description&, bool)`, the caller must (1) build an `options_description` object and populate it via `add_options()` with all long option names that should be recognized, and (2) have a valid, already-open `std::basic_istream<charT>` positioned at the start of the data to parse (e.g. a `std::stringstream` or `std::ifstream`). No other library-wide init call exists (no `boost::program_options::init()`).
**Evidence:**
> libs/program_options/include/boost/program_options/parsers.hpp:184-190: `template<class charT> ... basic_parsed_options<charT> parse_config_file(std::basic_istream<charT>&, const options_description&, bool allow_unregistered = false);`
> libs/program_options/src/parsers.cpp:108-120: target's first action is `const vector<shared_ptr<option_description> >& options = desc.options(); ... allowed_options.insert(d.long_name());` — it reads the already-built `options_description` to construct the whitelist.
> libs/program_options/example/config_file_types.cpp:81-110: `po::options_description opts; opts.add_options()("global_string", po::value<string>()) ... ;` built before `parse_config_file` is called at line 143.
> libs/program_options/example/multiple_sources.cpp:65-89: `options_description config_file_options; config_file_options.add(config).add(hidden); ... store(parse_config_file(ifs, config_file_options), vm);` — `options_description` assembled (even combined from sub-groups via `add()`) before the call.

### P2.2 Parameter Construction
**Claim:** Parameter 1 is a reference to an already-open `std::basic_istream<charT>` (callers typically use `std::stringstream`/`std::ifstream`, constructed and checked with `if (!strm)` before use in the filename-overload). Parameter 2 is a `const options_description&` that must contain **only long-named (or `name*` prefix) options**; options added with an empty long name (short-only, e.g. `(",o", ...)`) are rejected. Parameter 3 (`allow_unregistered`, default `false`) is a plain `bool` with no range constraints; `true` tolerates names not in the whitelist (sets `unregistered=true` instead of throwing), `false` is strict.
**Evidence:**
> libs/program_options/src/parsers.cpp:115-117: `if (d.long_name().empty()) boost::throw_exception(error("abbreviated option names are not permitted in options configuration files"));` — thrown before any stream reading if `options_description` has a short-only option.
> libs/program_options/test/exception_txt_test.cpp:539-543: `desc.add_options()(",o", value<string>(), "the output file"); istringstream is("o=output.txt\n"); store(parse_config_file(is, desc), vm);` is used specifically to trigger that `error` (test name `abbreviated_options_in_config_file`).
> libs/program_options/src/parsers.cpp:153-157 (filename overload): `std::basic_ifstream<charT> strm(filename); if (!strm) { boost::throw_exception(reading_file(filename)); }` — shows how the stream parameter is prepared/validated by the library itself in the convenience overload.
> libs/program_options/src/config_file.cpp:108-110: `bool registered = allowed_option(name); if (!registered && !m_allow_unregistered) boost::throw_exception(unknown_option(name));` — demonstrates effect of the `allow_unregistered` bool.

### P2.3 Object Lifecycle
**Claim:** `options_description` is value/stack-constructed (default ctor or captioned ctor), configured via `add_options()`, used read-only by `parse_config_file` (internally `desc.options()`), and it must outlive the returned `basic_parsed_options<charT>` because the result stores a raw `const options_description*` pointer (`description` member) — no ownership transfer, no explicit destroy call needed (ordinary C++ object lifetime via RAII/stack/scope).
**Evidence:**
> libs/program_options/include/boost/program_options/parsers.hpp:39-48: `basic_parsed_options(const options_description* xdescription, ...) ... const options_description* description; /** ... issues of lifetime are up to the caller. Can be NULL. */`
> libs/program_options/src/parsers.cpp:123: `parsed_options result(&desc);` — the function stores the *address* of the caller's `desc` parameter into the result, so `desc` must remain alive as long as the `basic_parsed_options` (and anything derived from it, e.g. via `store`) is used.
> libs/program_options/include/boost/program_options/options_description.hpp:191-231: `options_description` ctor + `add(shared_ptr<option_description>)`, `add(const options_description&)`, `options_description_easy_init add_options();` — standard stack object, no manual destroy.

### P2.4 Return Value Handling
**Claim:** Return value is a `basic_parsed_options<charT>` by value (never null/sentinel); errors are signaled exclusively via C++ exceptions, not return codes. Caller must wrap the call in try/catch (or let it propagate) and check for `boost::program_options::error`-derived exceptions: `reading_file` (file-open overload only), `unknown_option`, `invalid_config_file_syntax`, or a bare `error` (abbreviated-name / conflicting-wildcard-prefix cases).
**Evidence:**
> libs/program_options/src/parsers.cpp:102-130: function body always returns `basic_parsed_options<charT>(result)`, no error-code paths.
> libs/program_options/include/boost/program_options/errors.hpp:62-68: `class reading_file : public error { reading_file(const char* filename) ... };`
> libs/program_options/include/boost/program_options/errors.hpp:266, 335: `class unknown_option : public error_with_no_option_name`; `class invalid_config_file_syntax : public invalid_syntax`.
> libs/program_options/src/config_file.cpp:110,123: `boost::throw_exception(unknown_option(name));` and `boost::throw_exception(invalid_config_file_syntax(s, invalid_syntax::unrecognized_line));`
> libs/program_options/test/exception_txt_test.cpp:58-64: `try { if (style == -1) store(parse_config_file(is, desc), vm); ... } catch (...)` pattern used by the project's own tests.

### P2.5 Cleanup Sequence
**Claim:** No explicit free/close/destroy calls are needed for `parse_config_file`'s own objects: `basic_parsed_options<charT>` owns a `std::vector<basic_option<charT>>` that destructs normally; the `options_description` and the input stream are owned/destroyed by the caller via normal scope exit. If the filename overload is used, the internally-opened `std::basic_ifstream` is a local variable in `parse_config_file(const char*, ...)` and is destroyed (closing the file) automatically on return/throw — the caller does not manage it.
**Evidence:**
> libs/program_options/src/parsers.cpp:146-168: `std::basic_ifstream<charT> strm(filename); ... return result;` — `strm` is a stack/local object, destructed (and file closed) when the function returns or an exception unwinds.
> libs/program_options/include/boost/program_options/parsers.hpp:36-60: `basic_parsed_options` has only a `std::vector` and raw pointer members — standard-destructible, no custom dtor declared.

### P2.6 API Existence
**Claim:** All functions cited below exist verbatim in the current codebase.
**Evidence:**
> libs/program_options/include/boost/program_options/parsers.hpp:184-190 `parse_config_file(istream&, options_description&, bool)`
> libs/program_options/include/boost/program_options/parsers.hpp:205-207 `parse_config_file(const char*, options_description&, bool)`
> libs/program_options/include/boost/program_options/parsers.hpp:171-178 `parse_command_line(int, const charT* const[], options_description&, int, function1<...>)`
> libs/program_options/include/boost/program_options/options_description.hpp:231 `options_description_easy_init add_options();`
> libs/program_options/include/boost/program_options/variables_map.hpp:38-40 `void store(const basic_parsed_options<char>&, variables_map&, bool=false);`
> libs/program_options/include/boost/program_options/variables_map.hpp:53 `void notify(variables_map&);`
> libs/program_options/include/boost/program_options/detail/config_file.hpp:70-116 `common_config_file_iterator` + `get()`, `add_option()`, `allowed_option()`
> libs/program_options/include/boost/program_options/detail/config_file.hpp:118-138 `basic_config_file_iterator<charT>` + `getline()` + ctor.

### P2.7 Co-call Constraints
**Claim:** `parse_config_file` has no "begin/commit" pairing by itself, but it is conventionally paired downstream with `store()` then `notify()`: `store()` must be called on the `basic_parsed_options` before `notify()` is meaningful (notify reads values already merged into the `variables_map` by store). If the filename-opening overload is not used, the caller is responsible for opening/closing its own stream (open before call; no explicit close required after, but typical RAII idiom closes on scope exit).
**Evidence:**
> libs/program_options/example/config_file_types.cpp:143-146: `po::parsed_options parsed = parse_config_file(file, opts, ALLOW_UNREGISTERED); store(parsed, vm); ... notify(vm);`
> libs/program_options/example/multiple_sources.cpp:86-89: `store(parse_config_file(ifs, config_file_options), vm); notify(vm);`
> libs/program_options/test/exception_txt_test.cpp:60-63: `store(parse_config_file(is, desc), vm); ... notify(vm);`

### P2.8 Prerequisite State
**Claim:** The only state that must exist before calling the target is (a) a populated `options_description` (otherwise `allowed_options` is empty and every `name=value` line throws `unknown_option` unless `allow_unregistered=true`), and (b) a readable stream positioned at the data start. There is no global/process-wide program_options context, lock, or connection object.
**Evidence:**
> libs/program_options/src/parsers.cpp:108-120: whitelist (`allowed_options`) is built strictly from the `options_description` argument passed at call time — if empty, almost everything is "unregistered".
> libs/program_options/include/boost/program_options/parsers.hpp:184-190: declaration shows only 3 parameters, no required global/context object.

### P2.9 Result Usage
**Claim:** The returned `basic_parsed_options<charT>` (and its `.options` vector of `basic_option<charT>`) is consumed by `store(parsed_options, variables_map&, bool=false)`, which performs type validation/conversion per option using each option's `value_semantic`; after `store`, `notify(variables_map&)` runs any registered notifier callbacks and enforces `required_option` checks. Tests also pass `.options` directly to `collect_unrecognized()` to extract tokens of unregistered options (requires `allow_unregistered=true` when parsing, else `unregistered` flag is never set).
**Evidence:**
> libs/program_options/example/config_file_types.cpp:143-146: `parsed_options parsed = parse_config_file(file, opts, ALLOW_UNREGISTERED); store(parsed, vm); ... notify(vm);`
> libs/program_options/test/unrecognized_test.cpp:67-75: `vector<option> opts = parse_config_file(ss, desc, true).options; vector<string> result = collect_unrecognized(opts, include_positional);`
> libs/program_options/test/parsers_test.cpp:281-282, 292, 375-379: `vector<option> a1 = parse_config_file(ss, desc).options; BOOST_REQUIRE(a1.size() == 7); check_value(a1[0], "gv1", "0");` — directly inspects `.options[i].string_key`/`.value` without going through `store`.
> libs/program_options/src/variables_map.cpp:21-26: `void store(const parsed_options& options, variables_map& xm, bool utf8) { ... }` — the function signature that consumes the target's return value.

### P2.10 Behaviour Controls
**Claim:** Key behaviour-selecting inputs are: (1) `allow_unregistered` bool on the target itself — strict (throws `unknown_option`) vs. permissive (`unregistered=true` flag set, no throw); (2) option names ending in `*` in `options_description` (e.g. `"plug*"`) select the wildcard/prefix-matching branch in `add_option()`/`allowed_option()` instead of exact-set lookup, and conflicting overlapping prefixes (e.g. `"outpu*"` and `"outp*"`) throw a plain `error` at `add_option()` time; (3) short-only option names (empty long name) make `parse_config_file` throw immediately before any stream I/O; (4) `[section]` headers change the active `m_prefix` prepended to subsequent option names, nested via dotted names (`gui.accessibility.visual_bell`); (5) `#` starts a comment to end-of-line; lines with neither `[...]` nor `=` throw `invalid_config_file_syntax`.
**Evidence:**
> libs/program_options/src/config_file.cpp:108-110: branch on `allow_unregistered`.
> libs/program_options/src/config_file.cpp:39-66 (`add_option`): `if (*s.rbegin() == '*') { ... allowed_prefixes.insert(s); } ` plus bad-prefix-overlap `throw_exception(error(...))`.
> libs/program_options/test/exception_txt_test.cpp:514-520 (`config_file_wildcard`/similar): `desc.add_options()("outpu*", ...)("outp*", ...); ... store(parse_config_file(is, desc), vm);` used to trigger the conflicting-prefix error.
> libs/program_options/src/parsers.cpp:115-117: short-only name → immediate `error` before parsing.
> libs/program_options/src/config_file.cpp:96-102: `if (*s.begin() == '[' && *s.rbegin() == ']') { m_prefix = s.substr(1, s.size()-2); if (*m_prefix.rbegin() != '.') m_prefix += '.'; }`
> libs/program_options/src/config_file.cpp:91-94, 122-124: `#` comment stripping; `else { boost::throw_exception(invalid_config_file_syntax(s, invalid_syntax::unrecognized_line)); }`

### P2.11 Input Expectations
**Claim:** Input is plain-text, line-oriented INI-style: `getline()` reads one line at a time (std::getline on the `basic_istream`), '#' and everything after it on a line is stripped as comment, leading/trailing whitespace (" \t\r\n") trimmed, blank lines skipped. A line is either a `[section]` header (must start with `[` and end with `]` after trimming — no explicit handling of an unterminated `[section` other than falling through to the "no `=`" branch, which then throws `invalid_config_file_syntax`), or a `name = value` assignment (split at first `=`, both sides trimmed, value may be empty), or anything else throws `invalid_config_file_syntax`. No explicit max-line-length or max-file-size limit is enforced by this code (bounded only by `std::getline`/`std::string` limits). Option names are matched against the whitelist built from `options_description` after being prefixed with the current section (`m_prefix + name`).
**Evidence:**
> libs/program_options/src/config_file.cpp:83-125 (`common_config_file_iterator::get`): full line-processing loop with comment-stripping (`s.find('#')`), `trim_ws`, section-header branch, `=`-split branch, else-throw branch.
> libs/program_options/include/boost/program_options/detail/config_file.hpp:47-65: documented syntax conventions (comment char, `name = value`, `[section]`, dotted names).
> libs/program_options/src/config_file.cpp:166-176 (`basic_config_file_iterator<charT>::getline`): `if (std::getline(*is, in)) { s = to_internal(in); return true; } else { return false; }` — relies on stream's own `getline`, no explicit size cap.

## Other entries in the Logic Group

### options_description::add_options()
**Claim:** Must be called on a previously default/caption-constructed `options_description` object, before passing that `options_description` to `parse_config_file`/`parse_command_line`. Returns an `options_description_easy_init` whose `operator()` overloads append one `option_description` each (name[,short], optional `value_semantic*`, optional description string); chained calls build up the whitelist. Throws `duplicate_variable_error` (via `options_description::add`) if a name collides with an already-added one. Nothing to free (temporary helper object, no heap ownership exposed to caller beyond the `options_description` itself, which owns its `option_description`s via `shared_ptr`).
**Evidence:**
> libs/program_options/include/boost/program_options/options_description.hpp:208-211: `/** Adds new variable description. Throws duplicate_variable_error if either short or long name matches that of already present one. */ void add(shared_ptr<option_description> desc);`
> libs/program_options/include/boost/program_options/options_description.hpp:162-181 (`options_description_easy_init`): three `operator()` overloads (name+desc; name+value_semantic; name+value_semantic+desc).
> libs/program_options/example/config_file_types.cpp:84-110: `opts.add_options()("global_string", po::value<string>()) ("strings.word", po::value<string>()) ... ;` chained construction, long dotted names matching config-file section convention.
> **Combine with target:** build `options_description desc; desc.add_options()(...)...;` first, then call `parse_config_file(stream, desc, allow_unregistered)`.

### parse_command_line
**Claim:** Alternative/complementary entry producing the same `basic_parsed_options<charT>` type from `argc`/`argv` instead of a stream; requires a populated `options_description` (can allow short names, unlike the config-file target) and optional `style` flags / `ext` function for `=`-joined long options. Return value handled identically to the target (feed to `store`). Needs no special cleanup. In combined-source harnesses, typically called before or after the config-file parse into the *same* `variables_map`, with `store()` called separately for each source (the project's own rule: "first source wins" — `store` does not overwrite already-set non-defaulted values), so command-line-then-config-file order lets the command line override the config file in practice, matching `libs/program_options/example/multiple_sources.cpp`.
**Evidence:**
> libs/program_options/include/boost/program_options/parsers.hpp:171-178: `basic_parsed_options<charT> parse_command_line(int argc, const charT* const argv[], const options_description&, int style = 0, function1<...> ext = ext_parser());`
> libs/program_options/example/multiple_sources.cpp:75-77: `store(po::command_line_parser(ac, av).options(cmdline_options).positional(p).run(), vm); notify(vm);` then later config file is parsed and stored into the same `vm` (lines 86-89).
> libs/program_options/include/boost/program_options/variables_map.hpp:34-37 (doc comment on `store`): "If 'm' already has a non-defaulted value of an option, that value is not changed, even if 'options' specify some value." — explains override order when combining sources.

### store(basic_parsed_options<charT>&, variables_map&, bool utf8=false)
**Claim:** Direct consumer of the target's return value. Takes the `basic_parsed_options` (by const ref) and a `variables_map&` to fill; the `utf8` bool parameter is only relevant for the `wchar_t` overload path (internally converts). Must be called with a `variables_map` that is either freshly constructed or already partially filled by prior `store()` calls from other sources. No return value (void); errors surface as exceptions from value_semantic parsing (e.g. `invalid_option_value`) or `multiple_occurrences`. Nothing to explicitly free; `variables_map` is an ordinary stack object cleaned up by its destructor.
**Evidence:**
> libs/program_options/include/boost/program_options/variables_map.hpp:38-40: `BOOST_PROGRAM_OPTIONS_DECL void store(const basic_parsed_options<char>& options, variables_map& m, bool utf8 = false);`
> libs/program_options/src/variables_map.cpp:21-26: `void store(const parsed_options& options, variables_map& xm, bool utf8) { ... }`
> libs/program_options/test/exception_txt_test.cpp:499-503, 519-520, 530-531, 542-543: repeated pattern `store(parse_config_file(...), vm);` directly chaining target's result into `store`.

### notify(variables_map& m)
**Claim:** Must be called after one or more `store()` calls on the same `variables_map`; it is the point where `required_option` is checked (missing a `->required()` option throws here, not at `store` time) and any per-option notifier function objects (registered via `value_semantic`) are invoked with final values. Takes the `variables_map` by reference (mutated in place to mark notification state); void return. No explicit cleanup needed afterward beyond normal `variables_map` destruction.
**Evidence:**
> libs/program_options/include/boost/program_options/variables_map.hpp:52-53: `/** Runs all 'notify' function for options in 'm'. */ BOOST_PROGRAM_OPTIONS_DECL void notify(variables_map& m);`
> libs/program_options/example/config_file_types.cpp:144-146: `store(parsed, vm); ... notify(vm);` always called after `store`.
> libs/program_options/include/boost/program_options/errors.hpp:228-236: `class required_option : public error_with_option_name { required_option(const std::string& option_name) ... };` — the exception type notify/store-chain can raise for missing required options.

## Build notes
**Claim:** The project already ships (at the OSS-Fuzz level) a program_options fuzz target entry (`boost_programoptions_fuzzer.cc`) built as C++ with Boost headers found via `-I .` (the staged Boost source tree root), linked statically against the just-built `libboost_program_options.a`. The current checked-in `boost_programoptions_fuzzer.cc` is a stub placeholder, not a real P2-compliant harness — the downstream harness generator must write real `#include <boost/program_options.hpp>` (or the specific headers: `parsers.hpp`, `options_description.hpp`, `variables_map.hpp`) based code there.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/boost-c2/build.sh:25: `./b2 --user-config=user-config.jam --toolset=clang-ossfuzz link=static --with-headers --with-graph --with-filesystem --with-program_options headers stage;` — builds `program_options` as a static lib via b2.
> ${QF_WORK}/ossfuzz_view/boost-c2/build.sh:56: `$CXX $CXXFLAGS -I . ../boost_programoptions_fuzzer.cc $LIB_FUZZING_ENGINE -o boost_programoptions_fuzzer stage/lib/libboost_program_options.a` — C++ compile, include path `-I .` resolves to the staged Boost root so `#include <boost/program_options.hpp>` (and `.../parsers.hpp`) works, links `libboost_program_options.a` + the fuzzing engine.
> ${QF_WORK}/ossfuzz_view/boost-c2/boost_programoptions_fuzzer.cc:1-13: current file is only a stub `LLVMFuzzerTestOneInput` that ignores `data`/`size` and returns 0 — confirms no example of real usage is present in the OSS-Fuzz harness itself; the project's own `libs/program_options/example/*.cpp` and `libs/program_options/test/*.cpp` files (cited above) are the authoritative usage evidence instead.
> libs/program_options/include/boost/program_options.hpp (umbrella header) includes `parsers.hpp`, `options_description.hpp`, `variables_map.hpp` (confirmed by Glob listing showing all three exist under the same include dir and are the headers referenced throughout this report).
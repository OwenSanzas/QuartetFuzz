## target: boost::regex_replace

### P2.1 Init Sequence
**Claim:** Before calling `regex_replace`, the caller must (1) construct/compile a `basic_regex<charT,traits>` object (the pattern `e`) via a constructor or `assign()`/`set_expression()`, and (2) have the subject text available as a `[first,last)` iterator range (or a `std::basic_string` for the convenience overload). No other library-level global init is required. `basic_regex` compilation can throw `boost::regex_error` (derived from `std::runtime_error`) unless the `no_except` flag is used, in which case callers must check `status()`.
**Evidence:**
> libs/regex/include/boost/regex/v5/basic_regex.hpp:355-358: `explicit basic_regex(const charT* p, flag_type f = regex_constants::normal){ assign(p, f); }`
> libs/regex/test/regress/test_regex_replace.hpp:51-56: `r.assign(expression, syntax_options); if(r.status()){ ... } test_regex_replace(r);` — compile, check status, then call regex_replace.
> libs/regex/example/snippets/regex_replace_example.cpp:58-59,74: `e1.assign(expression_text); e2.assign(pre_expression); ... boost::regex_replace(oi, in.begin(), in.end(), e2, pre_format, boost::match_default | boost::format_all);`
> libs/regex/include/boost/regex/v5/regex_replace.hpp:25-27: header `#include <boost/regex/v5/basic_regex.hpp>` — `regex_replace` requires a fully-defined `basic_regex`.

### P2.2 Parameter Construction
**Claim:** Target signature: `OutputIterator regex_replace(OutputIterator out, BidirectionalIterator first, BidirectionalIterator last, const basic_regex<charT,traits>& e, Formatter fmt, match_flag_type flags = match_default)`.
- `out`: any model of OutputIterator (e.g. `std::ostream_iterator<char>`, `back_insert_iterator`, the library's internal `string_out_iterator`). Must be valid/writable for the whole call; no pre-sizing needed since it's an output iterator.
- `first,last`: a valid bidirectional-iterator range over the subject sequence of `charT`; `last` reachable from `first`. No NUL-termination requirement — range-based, not C-string based, in the iterator overload.
- `e`: a `basic_regex<charT,traits>` that must be successfully compiled (`e.status()==0`); if `e.flags() & failbit` the search logic degenerately fails (see regex_search.hpp:44 check used inside regex_iterator).
- `fmt`: a "Formatter" — can be a `const charT*`, `std::basic_string<charT>`, or a functor/function object; dispatched via `compute_functor_type` in `match_results::format`.
- `flags`: `match_flag_type` bitmask combining match-behavior bits (`match_default`, `match_not_bol`, `match_continuous`, `match_partial`, `match_any`, `match_posix`, ...) and format-behavior bits (`format_default`/`format_perl`=0, `format_sed`, `format_all`, `format_no_copy`, `format_first_only`).
**Evidence:**
> libs/regex/include/boost/regex/v5/regex_replace.hpp:31-37: full declaration as given.
> libs/regex/include/boost/regex/v5/match_flags.hpp:42-78: enumerators for match_* and format_* bits (match_default=0, format_perl=format_default=0, format_sed, format_all, format_no_copy, format_first_only).
> libs/regex/include/boost/regex/v5/match_results.hpp:358-368: `format(out, fmt, flags)` dispatches `fmt` through `compute_functor_type` — accepts strings or functors.

### P2.3 Object Lifecycle
**Claim:** `basic_regex` follows create → (optionally re-)configure via `assign` → use in `regex_replace`/`regex_search`/`regex_match` (read-only, const-ref) → destroy (RAII, `~basic_regex(){}`, backed by `shared_ptr<basic_regex_implementation>`). `regex_replace` itself has no object to create/destroy: it is a free function that, internally, constructs two stack-local `regex_iterator` objects (`i`, `j`) which drive `perl_matcher`/`regex_search`, and returns the updated `out` iterator — no handle outlives the call that needs explicit destruction by the caller.
**Evidence:**
> libs/regex/include/boost/regex/v5/basic_regex.hpp:369: `~basic_regex(){}` — trivial destructor, shared_ptr member handles cleanup (`m_pimpl` at line 653).
> libs/regex/include/boost/regex/v5/regex_replace.hpp:39-40: `regex_iterator<...> i(first,last,e,flags); regex_iterator<...> j;` — locals, scope-managed.
> libs/regex/include/boost/regex/v5/regex_iterator.hpp:85-86: `regex_iterator` holds a `std::shared_ptr<impl>` — automatic cleanup, no manual destroy call needed.

### P2.4 Return Value Handling
**Claim:** `regex_replace` returns the final `OutputIterator` position after all copies/substitutions (useful e.g. to know where in a `back_insert_iterator`'s container the output ends, or just typically discarded by callers who only care about the output container's contents). It does not return a status/bool. Errors surface as C++ exceptions thrown from inside the matcher/formatter machinery (`boost::regex_error`, or other `std::runtime_error`/`std::exception` derivatives), which callers must try/catch; `regex_replace` does not itself validate `e.status()` before running (that must be checked earlier on `e`).
**Evidence:**
> libs/regex/include/boost/regex/v5/regex_replace.hpp:62: `return out;` — only return value.
> libs/regex/example/snippets/regex_replace_example.cpp:57,84-85: `try{ ... boost::regex_replace(...); ...} catch(...) { return -1; }`
> libs/regex/test/regress/test_regex_replace.hpp:58-75: callers wrap compile+regex_replace in try/catch for `boost::bad_expression`, `std::runtime_error`, `std::exception`, `...`.
> libs/regex/include/boost/regex/v5/pattern_except.hpp:42-70: `regex_error : public std::runtime_error` with `raise()` calling `throw_exception`/`throw`.

### P2.5 Cleanup Sequence
**Claim:** No explicit free/close calls exist for any of `basic_regex`, `regex_iterator`, `match_results`, or the output container — all are RAII/value types backed by `shared_ptr` or STL containers that clean up on scope exit. The only "cleanup" a harness needs is to let `out`'s backing container (e.g. `std::string`, `std::ostringstream`) go out of scope normally, and to catch exceptions so no resource leak occurs from an unwound stack (RAII still runs during unwinding).
**Evidence:**
> libs/regex/include/boost/regex/v5/basic_regex.hpp:653: `std::shared_ptr<...> m_pimpl;` member — automatic lifetime.
> libs/regex/include/boost/regex/v5/regex_iterator.hpp:86: `typedef std::shared_ptr<impl> pimpl;`
> libs/regex/test/regex_replace_overflow.cpp:19-27: `std::ostringstream t(...); ... boost::regex_replace(oi, ...); std::string s(t.str()); BOOST_TEST(!s.empty());` — no explicit teardown calls.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current source tree at the cited paths/lines: `boost::regex_replace` (2 overloads), `boost::regex_merge` (4 overloads, pure forwarders to `regex_replace`), `boost::basic_regex::assign`/constructors/`status()`/`mark_count()`, `boost::regex_search` (4+ overloads), `boost::regex_match` (4+ overloads), `boost::regex_iterator`/`regex_iterator_implementation::init`/`next`, `boost::match_results::format`, `boost::BOOST_REGEX_DETAIL_NS::basic_regex_formatter::format_all/format_perl/format_conditional/format_until_scope_end`.
**Evidence:**
> libs/regex/include/boost/regex/v5/regex_replace.hpp:31,65
> libs/regex/include/boost/regex/v5/regex_merge.hpp:28,38,49,58
> libs/regex/include/boost/regex/v5/basic_regex.hpp:355,381,386,390,399,535
> libs/regex/include/boost/regex/v5/regex_search.hpp:29,38,54,63,72
> libs/regex/include/boost/regex/v5/regex_match.hpp:36,44,55,64,72
> libs/regex/include/boost/regex/v5/regex_iterator.hpp:48,60 (`init`, `next`)
> libs/regex/include/boost/regex/v5/match_results.hpp:359,385
> libs/regex/include/boost/regex/v5/regex_format.hpp:105,106,108,109 (declarations) and 207,286,652,737 (definitions)

### P2.7 Co-call Constraints
**Claim:** There is no lock/unlock-style pairing specific to `regex_replace`. The relevant pairing is compile-then-use: a `basic_regex` must be assigned/compiled (`assign`/constructor) before being passed by const-ref into `regex_replace`, `regex_search`, `regex_match`, or `regex_merge` — using an unassigned (default-constructed, `empty()==true`) `basic_regex` is a logic error (its `status()` returns `error_empty` and `get_first_state()` asserts non-null pimpl). There is no separate "finalize"/"close" call paired with compile.
**Evidence:**
> libs/regex/include/boost/regex/v5/basic_regex.hpp:525-528: `bool empty()const { return (m_pimpl.get() ? 0 != m_pimpl->status() : true); }`
> libs/regex/include/boost/regex/v5/basic_regex.hpp:619: `BOOST_REGEX_ASSERT(0 != m_pimpl.get());` in `get_first_state()` — asserts the regex was assigned before being walked by the matcher.
> libs/regex/include/boost/regex/v5/regex_search.hpp:44-45: `if(e.flags() & regex_constants::failbit) return false;` — guards against a bad/unassigned regex at the search entry used internally by `regex_replace`'s `regex_iterator`.

### P2.8 Prerequisite State
**Claim:** Prerequisite state for `regex_replace` is purely value-based: a compiled `basic_regex<charT,traits>` object `e` (with its chosen `flag_type` baked in from compile time, e.g. `regex_constants::normal/extended/perl/egrep/icase/nosubs/collate`), a valid `[first,last)` range, a valid format source `fmt`, and the `match_flag_type flags` argument. No global/thread-local context, no open connection/session, no locale setup beyond what the `traits` class already encapsulates (default `regex_traits<charT>` uses the global C++ locale unless `imbue()` was called on `e`).
**Evidence:**
> libs/regex/include/boost/regex/v5/regbase.hpp:84-92: syntax_option_type values `basic`, `extended=no_bk_refs|collate|no_perl_ex|no_escape_in_lists`, `normal=0`, `awk`, `grep`, `egrep`, `perl=normal`.
> libs/regex/include/boost/regex/v5/basic_regex.hpp:698-704: `imbue(locale_type l)` — optional locale configuration before compiling if non-default traits locale is needed.
> libs/regex/include/boost/regex/v5/regex_replace.hpp:39: `regex_iterator<...> i(first, last, e, flags);` — only these values are consumed, no external context.

### P2.9 Result Usage
**Claim:** `regex_replace`'s "result" is the data written through `out`, not a returned object. Real callers consume this output in two ways: (a) the `std::basic_string` overload returns the built string directly for further string processing; (b) the iterator-pair overload leaves transformed bytes in whatever container/stream backs `out` (e.g. `std::ostringstream`, `std::string` via `back_insert_iterator`/`string_out_iterator`), which callers then read via `.str()`, print, or re-feed as input to a second `regex_replace`/`regex_search` call (chained transform pipelines). `regex_merge` is a trivial pass-through wrapper, so callers of `regex_merge` consume its result identically.
**Evidence:**
> libs/regex/include/boost/regex/v5/regex_replace.hpp:71-74: `std::basic_string<charT> result; ...; regex_replace(i, s.begin(), s.end(), e, fmt, flags); return result;` — string overload returns the usable result directly.
> libs/regex/example/snippets/regex_replace_example.cpp:72-79: `std::ostringstream t(...); boost::regex_replace(oi, in.begin(), in.end(), e2, pre_format, ...); std::string s(t.str()); std::ostream_iterator<char> out(os); boost::regex_replace(out, s.begin(), s.end(), e1, format_string, ...);` — output of first `regex_replace` (`s`) becomes the input of the second `regex_replace` call (pipeline).
> libs/regex/test/regex_replace_overflow.cpp:19-27: `std::ostringstream t(...); boost::regex_replace(oi, ...); std::string s(t.str()); BOOST_TEST(!s.empty());` — result read via `.str()` and asserted non-empty.
> libs/regex/test/regress/test_regex_replace.hpp:32-36: `string_type result = boost::regex_replace(search_text, r, format_string, opts); if(result != result_string){ ... }` — string result compared against expected value.

### P2.10 Behaviour Controls
**Claim:** Multiple layers of flags select different processing paths:
1. **Compile-time `flag_type`** on `basic_regex` (`regex_constants::normal`, `extended`, `perl`=normal, `egrep`, `awk`, `basic`, `grep`, plus modifiers `icase`, `nosubs`, `collate`, `no_except`) select the regex-syntax grammar/parser used by `basic_regex_creator`, hence which bytes of the pattern are legal and what NFA is produced before `regex_replace` ever runs.
2. **`match_flag_type flags`** passed to `regex_replace` combines matching-phase bits (`match_not_bol`, `match_not_eol`, `match_continuous`, `match_partial`, `match_any`, `match_posix`, `match_perl`, `match_nosubs`, ...) that change how `regex_iterator`/`perl_matcher` searches, and formatting-phase bits consumed inside `regex_replace`'s own body and `match_results::format`→`basic_regex_formatter`: `format_no_copy` (regex_replace.hpp:43,51,59 — skip copying unmatched prefix/suffix), `format_first_only` (regex_replace.hpp:55-56 — `break` after first match), `format_sed`/`format_all`/`format_perl`(=0)/`format_default`(=0) which select the formatter's interpretation of `&`, `\N`, `$`, `(?1...)`, `\U\L\E` inside `fmt` (`format_all` enables the full conditional/case-folding mini-language gated by `BOOST_REGEX_MAX_RECURSION_DEPTH`=100).
3. **`Formatter fmt` type** — a literal `charT*`/`basic_string` is walked byte-by-byte as the mini expression language; a function-object `fmt` instead is invoked directly per match (`compute_functor_type` dispatch), bypassing the string-formatting mini-parser entirely.
**Evidence:**
> libs/regex/include/boost/regex/v5/regbase.hpp:74-92: flag bit values and syntax groups (`icase=1<<20`, `collate=1<<21`, `nosubs=1<<22`, `extended=...`, `normal=0`, `egrep=extended|newline_alt`, `perl=normal`).
> libs/regex/include/boost/regex/v5/match_flags.hpp:42-78: `match_default=0 ... match_posix ... format_perl=0 ... format_sed ... format_all ... format_no_copy ... format_first_only`.
> libs/regex/include/boost/regex/v5/regex_replace.hpp:43,51,55-56,59: direct checks of `format_no_copy` and `format_first_only` inside the target's body.
> libs/regex/include/boost/regex/v5/regex_format.hpp:227,260: `if((m_flags & boost::regex_constants::format_all) && (recursion_count < BOOST_REGEX_MAX_RECURSION_DEPTH))` gating conditional/`format_until_scope_end` vs plain `format_perl` paths.
> libs/regex/include/boost/regex/config.hpp:94: `#define BOOST_REGEX_MAX_RECURSION_DEPTH 100`.
> libs/regex/include/boost/regex/v5/match_results.hpp:365: `compute_functor_type<Functor, ...>::type F; F func(fmt); return func(*this, out, flags);` — dispatch differs by whether `fmt` is a string type or callable.

### P2.11 Input Expectations
**Claim:** The target itself imposes no format/header/termination requirement on `[first,last)` beyond "valid bidirectional-iterator range of `charT`" (not NUL-terminated; empty range is explicitly handled — produces a straight copy or nothing per `format_no_copy`). All structural constraints are enforced upstream, by the already-compiled `basic_regex e` (whatever syntax it accepts) and by the `fmt` formatter string, which is parsed by `basic_regex_formatter` as its own tiny recursive grammar reading `&`, `\\`, `(` , `)`, `:`, `?`, `$`, case-folding escapes `\U \L \E`, and Perl "verbs", bounded by `BOOST_REGEX_MAX_RECURSION_DEPTH` (100) to stop unbounded recursion from nested `(?1...)`/`(?2...)` conditionals. No explicit maximum length is checked on `first..last` or `fmt` within `regex_replace`/`basic_regex_formatter` other than this recursion-depth bound; overly long inputs/patterns are constrained only indirectly by the NFA engine limits.
**Evidence:**
> libs/regex/include/boost/regex/v5/regex_replace.hpp:39-61: handles empty-match case (`if(i==j)`) vs non-empty, with `format_no_copy` governing whether unmatched spans are copied — no other precondition on the range's contents.
> libs/regex/include/boost/regex/v5/regex_format.hpp:105-109,227,260: `format_all(recursion_count)`/`format_conditional(recursion_count)`/`format_until_scope_end(recursion_count)` recursive signatures bounded by `BOOST_REGEX_MAX_RECURSION_DEPTH`.
> libs/regex/include/boost/regex/config.hpp:94: recursion depth constant = 100.
> libs/regex/test/regex_replace_overflow.cpp:11,22-23: format string `"$2$2147483647"` used as a stress/regression input for the formatter's numeric backreference parsing, confirming attacker-controlled digit sequences in `fmt` are a recognized edge case.

### Other Logic-Group Entries — call protocol and combination with the target

**boost::regex_merge** (variant/forwarder, exists):
- Needs: identical prerequisites to `regex_replace` — a compiled `basic_regex`, an output iterator or none (string overload), `fmt` as `const charT*` or `std::basic_string<charT>`, optional `match_flag_type`.
- Behavior: pure one-line forwarding wrapper to `regex_replace`; no independent logic or distinct error paths.
- Return: same as the `regex_replace` it forwards to (`OutputIterator` or `std::basic_string<charT>`).
- Cleanup: none beyond what `regex_replace` requires.
- Combination with target: a harness may call `regex_merge` instead of/in addition to `regex_replace` on the same `(out/s, first, last, e, fmt, flags)` tuple to exercise the identical core through a second public entry point; found at libs/regex/include/boost/regex/v5/regex_merge.hpp:28-65 ("`return regex_replace(out, first, last, e, fmt, flags);`").

**boost::basic_regex::assign** (config/source, exists):
- Needs: nothing but a (possibly default-constructed) `basic_regex` object; called as `r.assign(pattern_begin, pattern_end, flag_type)` or `r.assign(const charT*, flag_type)`.
- Parameters/flags: `flag_type` (regex_constants::normal/extended/perl/egrep/awk/basic/grep, optionally OR'd with `icase`, `nosubs`, `collate`, `no_except`) selects the compiler/parser path in `basic_regex_creator` (not directly inspected here beyond its callers, but its effects are visible via `regbase.hpp` flag groups).
- Return: `basic_regex&` (self, for chaining); on failure throws `boost::regex_error` unless `no_except` was OR'd into `f`, in which case the error is recorded and must be read back via `status()` (basic_regex.hpp:600-609, 535-538).
- Must be called, and succeed (`status()==0`), before the regex is passed into `regex_replace`/`regex_search`/`regex_match`/`regex_merge`.
- Combination with target: this is the mandatory initializer for the `e` parameter of the target; a harness constructs/`assign`s `basic_regex` from fuzzer-controlled pattern bytes + a fuzzer-controlled `flag_type`, checks `status()`/catches `regex_error`, and only then calls `regex_replace`. Evidence: libs/regex/test/regress/test_regex_replace.hpp:51-56.

**boost::regex_search** (consumer/variant sharing perl_matcher core, exists):
- Needs: compiled `basic_regex`; `match_results<BidiIterator,Allocator>&` out-param (or none, for the boolean-only overload) must be default-constructible — no pre-fill required, it's an out-param that `regex_search` populates.
- Parameters/flags: `match_flag_type flags`, optional explicit `base` iterator (for `match_prev_avail`-style continued searches).
- Return: `bool` — whether a match was found; on true, `m` is filled with sub-match ranges (`m.mark_count()==e.mark_count()+1` per boost_regex_fuzzer.cc:26 assertion).
- Nothing to free (`match_results` is a value type with STL-backed storage).
- Combination with target: `regex_replace` is internally implemented on top of exactly this function via `regex_iterator_implementation::init/next` (regex_iterator.hpp:48-74, which calls `regex_search(first,end,what,re,flags[,base])`); a harness can call `regex_search` directly on the same `(text,e,flags)` to cross-check that `regex_replace`'s internal matching agrees with a direct `regex_search`/`match_results` inspection of the same regex and text.

**boost::regex_match** (consumer/variant sharing perl_matcher core, exists):
- Needs: compiled `basic_regex`; optional `match_results<>&` out-param.
- Parameters/flags: `match_flag_type flags`; unlike `regex_search`, requires the *whole* `[first,last)` to match (not a substring) — used by boost_regex_fuzzer.cc and boost_regex_pattern_fuzzer.cc as the companion full-match check after compiling a fuzzed pattern.
- Return: `bool`; fills `m` like `regex_search` on success.
- Nothing to free.
- Combination with target: exercises the same NFA engine (`perl_matcher`) used inside `regex_replace`'s per-match formatting step with a different driving mode (whole-string vs scanning for substrings+iteration+format); useful as a second, independent entry a harness calls on the same compiled regex/text pair. Evidence: libs/regex/include/boost/regex/v5/regex_match.hpp:36-43; boost_regex_fuzzer.cc:50-53; boost_regex_pattern_fuzzer.cc:43,49.

### Build notes
**Claim:** Fuzz harnesses for this project are C++ (not C). The target's header is reached via the umbrella `#include <boost/regex.hpp>` with the boost root added to the include path (`-I .` pointing at the staged boost checkout root, so `boost/regex.hpp` resolves to `libs/regex/include/boost/regex.hpp` after `b2 ... headers stage`); the regex library is linked by compiling its `.cpp` sources directly into the fuzzer binary (no separate static/shared lib needed for regex, unlike graph/filesystem/program_options which link `stage/lib/libboost_*.a`).
**Evidence:**
> ${QF_WORK}/src/boost-c3/build.sh:25: `./b2 --user-config=user-config.jam --toolset=clang-ossfuzz link=static --with-headers ... headers stage;` — stages headers so `#include <boost/regex.hpp>` resolves under the `-I .` root.
> ${QF_WORK}/src/boost-c3/build.sh:29-31: `$CXX $CXXFLAGS -I . ../boost_regex_fuzzer.cc libs/regex/src/*.cpp $LIB_FUZZING_ENGINE -o boost_regex_fuzzer` (and identical pattern for `boost_regex_pattern_fuzzer` and `boost_regex_replace_fuzzer`) — compiles the fuzzer `.cc` together with all of `libs/regex/src/*.cpp` (the compiled, non-template part of Boost.Regex) and the fuzzing engine, in one invocation, no intermediate `.a`.
> ${QF_WORK}/src/boost-c3/boost_regex_fuzzer.cc:19: `#include <boost/regex.hpp>` — confirms the umbrella header include path used by existing fuzzers.
> ${QF_WORK}/ossfuzz_view/boost-c3/Dockerfile:20-23: boost is git-cloned to `$SRC/boost`, then `build.sh` and the fuzzer `.cc` files are copied to `$SRC/` (one level above the boost tree, matching the harnesses' `../*.cc` reference in build.sh and the `-I .` = boost root).
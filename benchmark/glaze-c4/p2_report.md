## target: glz::minify_json

### Overview of the Logic Group entries
- `glz::minify_json(resizable auto& in)` — **target**. `include/glaze/json/minify.hpp:200-207`. Creates `context ctx{}` and `std::string out{}`, calls `detail::minify_json<Opts>(ctx, in, out)`, returns `out` (no error reported to caller).
- `glz::minify_json(resizable auto& in, auto& out)` — same core, caller-supplied `out`. `include/glaze/json/minify.hpp:193-198`.
- `glz::minify_jsonc(resizable auto& in)` — forces `Opts.comments = true` via `opt_true<Opts, &opts::comments>`, returns a `std::string`. `include/glaze/json/minify.hpp:216-223`.
- `glz::minify_jsonc(const auto& in, auto& out)` — same, caller-supplied `out`, `in` taken by `const auto&` (NOT `resizable auto&`). `include/glaze/json/minify.hpp:209-214`.

All four funnel into `glz::detail::minify_json<Opts>(is_context auto&& ctx, In&& in, Out&& out)` (`include/glaze/json/minify.hpp:156-186`), which in turn calls `glz::detail::minify_json<Opts>(ctx, it, end, out, ix)` (the character-dispatch loop, `include/glaze/json/minify.hpp:16-154`).

### P2.1 Init Sequence
**Claim:** No external library-level init call is required. Each public entry constructs its own `glz::context ctx{}` value-initialized on the stack immediately before calling into `detail::minify_json`; there is no separate "open"/"init" API that must run first.
**Evidence:**
> include/glaze/json/minify.hpp:196: `context ctx{};` (inside `minify_json(resizable auto& in, auto& out)`)
> include/glaze/json/minify.hpp:203: `context ctx{};` (inside `minify_json(resizable auto& in)`)
> include/glaze/json/minify.hpp:212, 219: same pattern in the two `minify_jsonc` overloads.
> include/glaze/core/context.hpp:141-145: `struct context { error_code error{}; ... };` — a simple aggregate, default-constructible, no setup needed.

### P2.2 Parameter Construction
**Claim:** `in` must be a type satisfying `resizable` (has `.resize(0)`), `contiguous` (has `.size()`/`.data()`), with 1-byte element type (`sizeof(decltype(*buffer.data())) == 1`), e.g. `std::string` or `std::vector<char>`. It is taken by non-const lvalue reference and is mutated in place (temporarily padded and un-padded) by `detail::minify_json`. For `minify_json(in, out)`/`minify_jsonc(in, out)`, `out` must satisfy `output_buffer` (a `range` of byte-sized element, non-const, and if resizable then `vector_like` — i.e. `resize()`, `operator[]`, `.data()`); typically `std::string` or `std::vector<char>`.
**Evidence:**
> include/glaze/json/minify.hpp:200-201: `template <auto Opts = opts{}> inline std::string minify_json(resizable auto& in)`
> include/glaze/concepts/container_concepts.hpp:37: `concept resizable = requires(T v) { v.resize(0); };`
> include/glaze/concepts/container_concepts.hpp:66: `concept contiguous = has_size<T> && has_data<T>;`
> include/glaze/core/read.hpp:41: `static_assert(sizeof(decltype(*buffer.data())) == 1);` inside `read_iterators`, enforced on every `in` passed through minify.
> include/glaze/concepts/container_concepts.hpp:317-319: `concept output_buffer = range<Buffer> && (sizeof(range_value_t<Buffer>) == sizeof(char)) && non_const_buffer<Buffer> && safe_resizable_buffer<Buffer>;`
> include/glaze/json/minify.hpp:156: `template <auto Opts, class In, output_buffer Out> requires(contiguous<In> && resizable<In>) inline void minify_json(...)`
> Opts is a compile-time `glz::opts` struct (default `opts{}`), with relevant fields `.comments` (default false) and `.null_terminated` (default `GLZ_NULL_TERMINATED`) — include/glaze/core/opts.hpp:69-74.

### P2.3 Object Lifecycle
**Claim:** There is no persistent "minifier" object; lifecycle is purely value-based per call: (1) `in` arrives already populated with JSON/JSONC text, (2) `detail::minify_json` grows `in` by `padding_bytes` (16) for scratch room, builds iterators, runs the scan loop writing into `out` (growing `out` as needed), (3) shrinks `out` to the written size if resizable, (4) restores `in` to its original size. No destroy call is needed beyond normal object destruction (RAII on `std::string`/`std::vector`).
**Evidence:**
> include/glaze/json/minify.hpp:160-163: `if (in.empty()) { return; } in.resize(in.size() + padding_bytes);`
> include/glaze/json/minify.hpp:165-167: `if constexpr (resizable<Out>) { out.resize(in.size() + padding_bytes); }`
> include/glaze/json/minify.hpp:182-185: `if constexpr (resizable<Out>) { out.resize(ix); } in.resize(in.size() - padding_bytes);`
> include/glaze/core/opts.hpp:31: `inline constexpr uint32_t padding_bytes = 16;`

### P2.4 Return Value Handling
**Claim:** `minify_json`/`minify_jsonc` (the public entries) do NOT propagate `ctx.error` to the caller — they discard it and always return a `std::string` (possibly empty on malformed/truncated input) or leave `out` as whatever got written. There is no error code to check at this public API level; callers that need error diagnostics must call `detail::minify_json<Opts>(ctx, in, out)` themselves and check `ctx.error`.
**Evidence:**
> include/glaze/json/minify.hpp:189-191 (comment): "We don't return errors from minifying even though they are handled because the error case should not happen since we minify auto-generated JSON. The detail version can be used if error context is needed"
> include/glaze/json/minify.hpp:170-172 (inside `detail::minify_json<Opts>(ctx,in,out)`): `if (bool(ctx.error)) [[unlikely]] { return; }` — only checked internally to abort early, not surfaced.
> include/glaze/json/minify.hpp:148-151 (inside the character-dispatch loop): `default: { ctx.error = error_code::syntax_error; return; }` — on malformed/unterminated input the loop just stops; whatever was dumped into `out`/`b` so far remains, then gets resized to `ix` in the resizable-Out path.
> tests/json_test/json_test.cpp:13599-13610: `"invalid minify"_test` — malformed input `"\""` (lone quote) yields `minified == ""`; `"f"` yields `"false"` (because `*it=='f'` dispatches unconditionally to the Bool branch without checking the rest of "false" matches) — demonstrates the function does not fail loudly, it just stops/under-produces on bad input.

### P2.5 Cleanup Sequence
**Claim:** No manual free/close is required; `in`/`out`/`ctx` are stack-local RAII objects. The only required "cleanup" performed by the library itself is the padding un-resize of `in` at minify.hpp:185, which happens unconditionally after the dispatch loop returns (including on the error path, since the loop simply `return`s out of `detail::minify_json<Opts>(ctx,it,end,b,ix)` and control falls through to the un-resize statement in the caller `detail::minify_json<Opts>(ctx,in,out)`).
**Evidence:**
> include/glaze/json/minify.hpp:168-185: single straight-line sequence; the inner dispatch loop at minify.hpp:16 returns via `break`-out-of-while or the `default: ctx.error=...; return;`, and execution resumes at line 182 (`out.resize(ix)` then `in.resize(in.size()-padding_bytes)`), so padding is always removed from `in` regardless of whether an error occurred.

### P2.6 API Existence
**Claim:** All functions cited below exist verbatim in the current tree (`include/glaze/json/minify.hpp`, `include/glaze/json/json_format.hpp`, `include/glaze/core/read.hpp`, `include/glaze/util/dump.hpp`, `include/glaze/core/opts.hpp`, `include/glaze/core/context.hpp`).
**Evidence:**
> `glz::minify_json(resizable auto& in)` — include/glaze/json/minify.hpp:200-207
> `glz::minify_json(resizable auto& in, auto& out)` — include/glaze/json/minify.hpp:193-198
> `glz::minify_jsonc(resizable auto& in)` — include/glaze/json/minify.hpp:216-223
> `glz::minify_jsonc(const auto& in, auto& out)` — include/glaze/json/minify.hpp:209-214
> `glz::detail::minify_json<Opts>(ctx, in, out)` — include/glaze/json/minify.hpp:156-186
> `glz::detail::minify_json<Opts>(ctx, it, end, b, ix)` — include/glaze/json/minify.hpp:14-154
> `glz::read_iterators<Opts, Padded>(buffer)` — include/glaze/core/read.hpp:38-54
> `glz::detail::read_json_string<Opts>(it, end)` (padded overload, `requires(check_is_padded(Opts))`) — include/glaze/json/json_format.hpp:73-105
> `glz::detail::read_json_string<Opts>(it, end)` (non-padded overload, `requires(!check_is_padded(Opts))`) — include/glaze/json/json_format.hpp:107-...
> `glz::detail::read_json_number<null_terminated>(it, end)` — include/glaze/json/json_format.hpp:195-210
> `glz::detail::read_jsonc_comment(it, end)` — include/glaze/json/json_format.hpp:157-193
> `glz::dump<Checked>(c, b, ix)` / `glz::dump<false>(str, b, ix)` — include/glaze/util/dump.hpp:80-123 (ns `glz`, called unqualified from `glz::detail`, found via ADL/namespace lookup)
> `glz::dump_maybe_empty<Checked>(str, b, ix)` — include/glaze/util/dump.hpp:250
> `glz::context` — include/glaze/core/context.hpp:141-145
Grep confirms no stale/duplicate definitions interfering: `Grep(pattern="minify_json|minify_jsonc")` hit only `include/glaze/json/minify.hpp` plus test files.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for minify. The only "paired" behaviour is internal and automatic: `in.resize(+padding_bytes)` at minify.hpp:163 is always paired with `in.resize(-padding_bytes)` at minify.hpp:185 within the same call (not caller-visible), and (for resizable `Out`) `out.resize(in.size()+padding_bytes)` at minify.hpp:166 is paired with `out.resize(ix)` at minify.hpp:183. Callers do not need to call any "begin"/"end" function themselves.
**Evidence:**
> include/glaze/json/minify.hpp:163 and :185 (grow/shrink `in`)
> include/glaze/json/minify.hpp:166 and :183 (grow/shrink `out`, resizable-only branch)

### P2.8 Prerequisite State
**Claim:** No connections, global config, or registry state is required. The only prerequisite is that `in` already contains the bytes to minify (populated by the caller, e.g. from fuzzer input) before the call; `glz::opts` template parameter `Opts` is a compile-time constant selected by the caller (default `opts{}`), not runtime state.
**Evidence:**
> include/glaze/json/minify.hpp:200-201: `in` is the sole runtime parameter; `Opts` is a non-type template parameter (`template <auto Opts = opts{}>`).
> include/glaze/core/opts.hpp:69-74: `struct opts { ...; bool null_terminated = GLZ_NULL_TERMINATED; bool comments = false; ... };` — plain compile-time struct, no runtime construction/init call.

### P2.9 Result Usage
**Claim:** The returned `std::string` (or filled `out` buffer) from `minify_json`/`minify_jsonc` is consumed, in the project's own tests, by: (a) direct `std::string` equality comparisons against an expected minified form, and (b) re-parsing it with `glz::read<Opts>`/`glz::read_json`/`glz::read_jsonc` using `Opts.minified = true` (an optimization flag telling the reader the input is already minified so it can skip some whitespace handling). No other glaze API treats the result specially; it is just a `std::string`/buffer like any other JSON text.
**Evidence:**
> tests/json_test/json_test.cpp:2165-2168: `const auto minified = glz::minify_json(thing_pretty); expect(json == minified); const auto ec = glz::read<glz::opts{.minified = true}>(obj, minified); expect(!ec) << glz::format_error(ec, minified);`
> tests/json_test/json_test.cpp:2268-2270: `const auto minified = glz::minify_jsonc(thing_pretty); expect(json == minified); expect(!glz::read_jsonc(obj, minified));`
> tests/json_test/json_test.cpp:2774-2776: `std::string minified = glz::minify_json(prettified); expect(!glz::read_json(obj, minified)); expect(buffer == minified);`
> tests/json_test/json_test.cpp:2783-2786: `std::string minified = glz::minify_json(prettified); expect(!glz::read<glz::opts{.minified = true}>(obj, minified)); expect(buffer == minified);`
> tests/json_test/json_test.cpp:4417-4420: `std::string minified = glz::minify_json(output_json); expect(minified.contains("test")); ...` — substring checks, i.e. treated as opaque text.
> tests/example_json/example_json.cpp:337-338: `std::string minified = glz::minify_json(pretty); expect(minified == json);`

### P2.10 Behaviour Controls
**Claim:** Behaviour is selected entirely at compile time via the `Opts` non-type template parameter and the choice of entry function:
- `minify_json` vs `minify_jsonc`: `minify_jsonc` sets `Opts.comments=true` via `opt_true<Opts, &opts::comments>` before dispatch, routing `/` bytes into `read_jsonc_comment`; `minify_json` leaves `comments=false`, so `/` falls through to `default: ctx.error = error_code::syntax_error; return;` (minify.hpp:135-151).
- `Opts.null_terminated` (default `GLZ_NULL_TERMINATED`): selects, in every scan loop (`skip_whitespace`, `skip_expected_whitespace`, the top-level `while`, and `read_json_number<null_terminated>`), between an unconditional `while(table[*it]) ++it;` walk (relies on a sentinel byte) and an explicit `(it < end) && table[*it]` bounds-checked walk.
- `string_t<In>`: `detail::minify_json<Opts>(ctx,in,out)` picks `opt_true<O,&opts::null_terminated>` when `In` is a `string_t` (assumes it is null-terminated after the padding resize) vs `opt_false<...>` otherwise (minify.hpp:174-180).
- `is_padded_on<Opts>()`: controls which `read_json_string<Opts>` overload is selected via `requires(check_is_padded(Opts))` / `requires(!check_is_padded(Opts))` — the padded overload trusts there are at least `padding_bytes` safely readable past the logical end (set by `read_iterators<Opts, true>(in)` subtracting `padding_bytes` from `end`), the non-padded overload stops its 8-byte chunked loop at `end-7` and falls back to a presumably byte-wise tail handling (truncated in the 4000-char read above but structurally mirrored in `read_jsonc_comment`, json_format.hpp:161-192).
- `resizable<Out>`: in `detail::minify_json<Opts>(ctx,in,out)`, if `Out` (e.g. `std::string`) is resizable, `out` is pre-grown and then shrunk to the exact written size (`ix`); if not resizable (e.g. a fixed `std::span`), no resize occurs and caller is responsible for giving a large-enough buffer up front.
**Evidence:**
> include/glaze/json/minify.hpp:135-151 (Comment branch / fallthrough to syntax_error)
> include/glaze/json/minify.hpp:213, 221 (`opt_true<Opts, &opts::comments>` in both `minify_jsonc` overloads)
> include/glaze/json/minify.hpp:29-38, 44-53, 58-65 (`Opts.null_terminated` branches in the lambdas and main loop)
> include/glaze/json/json_format.hpp:195-209 (`read_json_number<null_terminated>` branch)
> include/glaze/json/minify.hpp:174-180 (`string_t<In>` selecting `opt_true`/`opt_false` on `null_terminated`)
> include/glaze/json/json_format.hpp:73-75 and :107-109 (`requires(check_is_padded(Opts))` / `requires(!check_is_padded(Opts))` overloads of `read_json_string`)
> include/glaze/core/read.hpp:46-51 (`read_iterators<Opts,Padded>`: `end += buffer.size() - padding_bytes` when `Padded`)
> include/glaze/json/minify.hpp:165-167, 182-184 (`resizable<Out>` branch)

### P2.11 Input Expectations
**Claim:** `minify_json`/`minify_jsonc` expect `in` to be a contiguous, 1-byte-element, resizable byte buffer (not a `const` view — it is mutated/padded in place), containing JSON or JSONC text. There is no minimum size requirement enforced other than "not empty" (the function returns immediately, leaving `out` untouched, if `in.empty()`). The scanner works on raw bytes and performs unchecked 8-byte `memcpy` reads inside `read_json_string`/`read_jsonc_comment`/number scanning assuming the padded iterators give `padding_bytes`(16) bytes of safe slack past the logical end — this slack is created by the library itself (`in.resize(in.size()+padding_bytes)`), not something the caller must pre-allocate. Malformed/truncated/unterminated strings, comments, or tokens do not throw; the scan loop simply stops (returns an empty `sv{}` from `read_json_string`/`read_jsonc_comment`, or lands on `ctx.error=syntax_error` for unrecognized leading bytes), and whatever was already written to `out` up to that point is kept (truncated output), per the `"invalid minify"_test` cases.
**Evidence:**
> include/glaze/json/minify.hpp:160-162: `if (in.empty()) { return; }` — only size check; no explicit minimum beyond not-empty.
> include/glaze/json/json_format.hpp:79-104 (padded `read_json_string`): unconditional `std::memcpy(&chunk, it, 8)` inside `while (it < end)`, relying on `end` being short by `padding_bytes` of the true allocation (read.hpp:47) so the 8-byte read never runs past the real buffer.
> include/glaze/json/json_format.hpp:102-104: `return {};` when the scan runs off the (padded) end without finding a closing quote — unterminated string silently yields empty `sv`.
> include/glaze/json/json_format.hpp:183-192: tail-of-buffer byte-wise fallback loop in `read_jsonc_comment`, also returning `{}` if no closing `*/` is found.
> tests/json_test/json_test.cpp:13599-13610: concrete evidence that truncated/malformed inputs (`"f"`, `"\""`, `"\" "`) produce defined (not crashing) results — `"false"` or `""`.
> include/glaze/json/minify.hpp:6: "Minified JSONC only works with /**/ style comments, so we only support this" — documents the only supported comment style.

### Build notes
**Claim:** Glaze is a header-only C++ library; fuzz harnesses are plain C++23 translation units compiled directly against the `include/` tree with no separate static/shared library to link, and (per `fuzzing/ossfuzz.sh`) every file in `fuzzing/*.cpp` except ones matching `exhaustive` or `main.cpp` is built into its own OSS-Fuzz target.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/glaze-c4/build.sh:18: `fuzzing/ossfuzz.sh`
> fuzzing/ossfuzz.sh:11-16:
> ```
> for SRCFILE in $(ls fuzzing/*.cpp |grep -v -E '(exhaustive|main\.cpp)'); do
>     NAME=$(basename $SRCFILE .cpp)
>     $CXX $CXXFLAGS -std=c++23 -g -Iinclude \
>          $SRCFILE -o $OUT/$NAME \
>          $LIB_FUZZING_ENGINE
> done
> ```
> i.e. C++23, header search path `-Iinclude` (project root `include/` — so a harness should `#include <glaze/glaze.hpp>` or the narrower `#include <glaze/json/minify.hpp>`), linked only against `$LIB_FUZZING_ENGINE` (no other library — glaze is header-only).
> include/glaze/json.hpp:19: `#include "glaze/json/minify.hpp"` — confirms `minify.hpp` is reachable via the umbrella `glaze/json.hpp` (and transitively `glaze/glaze.hpp`).
> There is already a dedicated fuzzer slot `fuzzing/json_minify.cpp`, but it currently contains a stub (`fuzzing/json_minify.cpp:1`: "Gold harness replaced by an empty stub for baseline generation.") that must be replaced by the real minify harness; `fuzzing/json_with_comments.cpp` and `fuzzing/json_prettify.cpp` show the established harness pattern (vector-of-bytes input, optional null-terminator push_back, calling the glz entry point, discarding the result with `[[maybe_unused]]`) that a `minify_json`/`minify_jsonc` harness should follow.
## target: glz::prettify_json

### P2.1 Init Sequence
**Claim:** No global/library init is required. `prettify_json` constructs its own `glz::context` internally and runs standalone; the only "setup" a caller does is produce the `in` buffer (e.g. via `glz::write_json`) before calling it. There is no required call-before relationship with any other glaze API except in round-trip usage where `write_json` produces the JSON that gets prettified.
**Evidence:**
> include/glaze/json/prettify.hpp:229-236:
> ```
> template <auto Opts = opts{}>
> inline std::string prettify_json(const auto& in)
> {
>    context ctx{};
>    std::string out{};
>    detail::prettify_json<Opts>(ctx, in, out);
>    return out;
> }
> ```
> tests/json_test/json_test.cpp:309-311: `expect(not glz::write_json(s, buffer)); ... auto pretty = glz::prettify_json(buffer);` — write then prettify is the common pattern, but not an enforced precondition (prettify_json accepts arbitrary bytes).
> tests/exceptions_test/exceptions_test.cpp:50: `glz::prettify_json(buffer)` called directly on a literal buffer with no prior glaze call.

### P2.2 Parameter Construction
**Claim:** `in` must satisfy the `contiguous` concept (has `.data()`/`.size()`, 1-byte element type) — `std::string`, `std::string_view`, `std::vector<char>`, `std::span<char>` all qualify. No other parameter is required for the allocating overload; `Opts` is a template value (defaults to `opts{}`) selecting formatting behavior, not a runtime parameter.
**Evidence:**
> include/glaze/core/read.hpp:38-41: `template <auto Opts, bool Padded = false> auto read_iterators(contiguous auto&& buffer) noexcept { static_assert(sizeof(decltype(*buffer.data())) == 1); ... }`
> include/glaze/concepts/container_concepts.hpp:66: `concept contiguous = has_size<T> && has_data<T>;`
> include/glaze/json/prettify.hpp:229-230: `template <auto Opts = opts{}> inline std::string prettify_json(const auto& in)` — `in` is taken by const reference, unconstrained by concept at this layer (constraint enforced transitively through `read_iterators`/`detail::prettify_json` requiring `contiguous`).

### P2.3 Object Lifecycle
**Claim:** There is no persistent "object" to create/destroy: `prettify_json` is a pure function — construct input buffer, call, use returned `std::string`, let it go out of scope. The buffer-writing overload (`prettify_json(in, out)`) similarly just needs an already-existing `out` container satisfying `output_buffer`; no separate init/teardown API exists.
**Evidence:**
> include/glaze/json/prettify.hpp:219-224:
> ```
> template <auto Opts = opts{}>
> inline void prettify_json(const auto& in, auto& out)
> {
>    context ctx{};
>    detail::prettify_json<Opts>(ctx, in, out);
> }
> ```
> include/glaze/json/prettify.hpp:186-212 (`detail::prettify_json(ctx, in, out)`): internally builds a local `std::vector<json_type> state(64)` stack and `int64_t indent` that live only for the call duration — no handle escapes.

### P2.4 Return Value Handling
**Claim:** The public `prettify_json` entries **swallow errors** — they never surface `ctx.error` to the caller. The allocating overload always returns a `std::string` (possibly truncated/partial if `ctx.error` was set internally, since the detail function `return`s early on error leaving `out`/`ix` in whatever state they had reached). Callers that need error visibility must call `glz::detail::prettify_json<Opts>(ctx, in, out)` directly and inspect `ctx.error`.
**Evidence:**
> include/glaze/json/prettify.hpp:215-217: "// We don't return errors from prettifying even though they are handled because the error case should not happen since we prettify auto-generated JSON. // The detail version can be used if error context is needed"
> include/glaze/json/prettify.hpp:74-77 and 96-99 and 132-135 and 152-155 and 178-181 (detail::prettify_json): every error path does `ctx.error = error_code::...; return;` without any indication reaching the public wrapper.
> include/glaze/json/prettify.hpp:198-200 (buffer overload): `if (bool(ctx.error)) [[unlikely]] { return; }` after `read_iterators` — again silently stops, `out` left as whatever `resize(in.size()*2)` produced.

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup is required; all state is RAII (local `std::string`, local `std::vector<json_type>`). On error the function just returns early — no leaked resources, but the output buffer may be partially written / not finally `resize()`d to the true length in the buffer-overload (see below), so a harness should not assume `out` is well-formed JSON after an error.
**Evidence:**
> include/glaze/json/prettify.hpp:209-211: `if constexpr (resizable<Out>) { out.resize(ix); }` — this only runs when control falls through normally after `detail::prettify_json` (it is NOT guarded by `ctx.error`, so it does run even after errors, but `ix` reflects only what was written before the error).
> include/glaze/json/prettify.hpp:22-23 (detail::prettify_json core): `std::vector<json_type> state(64); int64_t indent{};` — stack-allocated, destroyed automatically at end of the (templated) function.

### P2.6 API Existence
**Claim:** All the functions referenced below exist in the current codebase.
**Evidence:**
> include/glaze/json/prettify.hpp:230 `glz::prettify_json(const auto& in)`
> include/glaze/json/prettify.hpp:220 `glz::prettify_json(const auto& in, auto& out)`
> include/glaze/json/prettify.hpp:239 `glz::prettify_jsonc(const auto& in, auto& out)`
> include/glaze/json/prettify.hpp:249 `glz::prettify_jsonc(const auto& in)`
> include/glaze/json/minify.hpp:194 `glz::minify_json(resizable auto& in, auto& out)`
> include/glaze/json/minify.hpp:201 `glz::minify_json(resizable auto& in)`
> include/glaze/json/minify.hpp:210 `glz::minify_jsonc(const auto& in, auto& out)`
> include/glaze/json/minify.hpp:217 `glz::minify_jsonc(resizable auto& in)`
> include/glaze/json/prettify.hpp:15, 186 `glz::detail::prettify_json` (two overloads: core loop, and buffer-driving wrapper)
> include/glaze/json/minify.hpp:16, 158 `glz::detail::minify_json` (same pattern)
> include/glaze/json/json_format.hpp:75,109 `glz::detail::read_json_string` (padded/unpadded overloads)
> include/glaze/json/json_format.hpp:196 `glz::detail::read_json_number`
> include/glaze/json/json_format.hpp:157 `glz::detail::read_jsonc_comment`
> include/glaze/json/json_format.hpp:62 `glz::detail::append_new_line`
> include/glaze/core/read.hpp:39 `glz::read_iterators`

### P2.7 Co-call Constraints
**Claim:** There is no lock/unlock or begin/commit pairing. The only "paired" relationship observed in the project's own tests is the round-trip pattern `prettify_json` → `minify_json` (and `prettify_jsonc` → `minify_jsonc`) used to validate that prettifying didn't corrupt the JSON, and `write_json`/`write_jsonc` → `prettify_json`/`prettify_jsonc` → `read_json` as a correctness chain. None of these are enforced by the API (each function works standalone on raw bytes).
**Evidence:**
> tests/json_test/json_test.cpp:2165-2168: `const auto minified = glz::minify_json(thing_pretty); expect(json == minified); const auto ec = glz::read<glz::opts{.minified = true}>(obj, minified);`
> tests/json_test/json_test.cpp:2770-2776 ("minified_custom_object"): `std::string buffer = glz::write_json(obj)...; std::string prettified = glz::prettify_json(buffer); std::string minified = glz::minify_json(prettified); expect(!glz::read_json(obj, minified)); expect(buffer == minified);`
> tests/json_test/json_test.cpp:2792-2796 ("prettified_custom_object"): `buffer = glz::prettify_json(buffer); expect(glz::read_json(obj, buffer) == glz::error_code::none);`
> tests/json_test/json_test.cpp:2268: `const auto minified = glz::minify_jsonc(thing_pretty);` paired with `prettify_jsonc` at line 2176 (via recall of surrounding suite, see P2.9).

### P2.8 Prerequisite State
**Claim:** No global/connection/config state is required before calling `prettify_json`. The only prerequisite is a readable contiguous byte buffer (`in`); it does not have to be well-formed JSON — the tokenizer is designed to detect malformed input via `ctx.error` (though that error is discarded by the public wrapper, see P2.4). `Opts` is a compile-time template parameter, not runtime state, so there's no "configuration object" to build first (unlike, e.g., glz readers that need a populated `glz::meta<T>`).
**Evidence:**
> include/glaze/json/prettify.hpp:229-236 (allocating overload) takes only `in`, no external state.
> include/glaze/json/prettify.hpp:15 `inline void prettify_json(is_context auto&& ctx, auto&& it, auto&& end, auto&& b, auto& ix)` — `ctx` is locally constructed (`context ctx{};` at line 232), not shared/global.
> include/glaze/core/context.hpp:141-154 `struct context { error_code error{}; ... };` — default-constructible, no setup needed.

### P2.9 Result Usage
**Claim:** The returned `std::string` (or filled `out` buffer) from `prettify_json`/`prettify_jsonc` is consumed by: (a) direct string comparison/printing in tests, (b) `glz::read_json`/`glz::read<Opts>` to re-parse the prettified text back into a struct (round trip correctness), (c) `glz::minify_json`/`glz::minify_jsonc` to strip it back down, whose own output is then compared against the pre-prettify buffer and/or fed to `glz::read_json`.
**Evidence:**
> tests/json_test/json_test.cpp:2795-2796: `buffer = glz::prettify_json(buffer); expect(glz::read_json(obj, buffer) == glz::error_code::none);` — result fed straight into `read_json`.
> tests/json_test/json_test.cpp:2773-2776: `std::string prettified = glz::prettify_json(buffer); std::string minified = glz::minify_json(prettified); expect(!glz::read_json(obj, minified)); expect(buffer == minified);` — result piped through minify then read_json then compared.
> tests/json_test/json_test.cpp:2074 + 2163: `glz::prettify_json(json, buffer); ... expect(thing_pretty == buffer);` — buffer-overload result compared against expected literal.
> tests/example_json/example_json.cpp:335: `std::string pretty = glz::prettify_json(json);` (used for display/printing, downstream use not parsing).
> tests/networking_tests/openapi_test/openapi_test.cpp:182: `std::cout << glz::prettify_json(response.response_body) << std::endl;` — printed directly, no further API consumes it.

### P2.10 Behaviour Controls
**Claim:** `Opts` (a compile-time value of type `glz::opts` or a user-derived struct) controls: `Opts.comments` (false by default; `prettify_jsonc`/`minify_jsonc` force it true via `opt_true<Opts, &opts::comments>`, enabling the `Comment` branch / `read_jsonc_comment` instead of falling through to `syntax_error`); `Opts.new_lines_in_arrays` (default true; false collapses array formatting — checked via `check_new_lines_in_arrays`); `Opts.null_terminated` (automatically set true/false depending on whether `In` is `string_t` vs not, inside `detail::prettify_json`/`detail::minify_json`, selecting between bounds-checked-by-`end` and null-byte-terminated scan loops); `check_indentation_char`/`check_indentation_width` only affect output bytes. These are all resolved at compile time via `if constexpr`.
**Evidence:**
> include/glaze/json/prettify.hpp:163-172: `case Comment: { if constexpr (Opts.comments) { const auto value = read_jsonc_comment(it, end); ... break; } else { [[fallthrough]]; } }` followed by the `default: ctx.error = error_code::syntax_error;` path at line 178-181 when comments are off.
> include/glaze/json/prettify.hpp:239,249: `detail::prettify_json<opt_true<Opts, &opts::comments>>(ctx, in, out);` (prettify_jsonc forcing comments on).
> include/glaze/json/prettify.hpp:35-50, 80-91: `if constexpr (check_new_lines_in_arrays(Opts))` branches for Comma and Array_Start.
> include/glaze/json/prettify.hpp:202-207: `if constexpr (string_t<In>) { prettify_json<opt_true<Opts, &opts::null_terminated>>(...); } else { prettify_json<opt_false<Opts, &opts::null_terminated>>(...); }` — selects null-terminated vs bounds-checked loop variant based on `In`'s type, not a user-set flag.
> include/glaze/core/opts.hpp:690-714: `check_indentation_char`, `check_indentation_width`, `check_new_lines_in_arrays` consteval accessors with defaults.
> tests/json_test/json_test.cpp:49-52, 323: `struct opts_no_array_newlines : glz::opts { bool new_lines_in_arrays = false; }; ... glz::prettify_json<opts_no_array_newlines{}>(buffer);` — demonstrates a user-supplied Opts struct overriding the field.

### P2.11 Input Expectations
**Claim:** `in` is treated as a raw byte buffer, not required to be valid JSON beforehand (prettify re-tokenizes from scratch via `json_types[uint8_t(*it)]` classification). It must be a `contiguous` range (`.data()`/`.size()`), 1-byte element type (`static_assert(sizeof(decltype(*buffer.data())) == 1)`). There's no explicit minimum/maximum size check beyond the recursion-depth guard (`max_recursive_depth_limit = 256`, checked when the per-call `state` vector doubles past it) and the hard-coded `/* comment */`-style-only comment support. Malformed bytes drive `ctx.error = error_code::syntax_error` (unknown leading byte) or `error_code::exceeded_max_recursive_depth` (over-nested `[`/`{`), but as shown in P2.4 these are discarded by the public wrapper. `read_json_string`/`read_json_number`/`read_jsonc_comment` scan in 8-byte SIMD-ish chunks and can run past `end` by up to 7 bytes in the "padded" overload (`check_is_padded(Opts)` requires caller-supplied slack) — for the plain `prettify_json` entry (not going through `read`'s padded buffer machinery) the **unpadded** overload of `read_json_string` is selected, which explicitly handles the `end-7` tail loop to avoid overrun; still, an unterminated string/comment causes `it` to run to `end` and return `{}` (empty value) rather than erroring, which the caller-visible API never flags.
**Evidence:**
> include/glaze/core/read.hpp:39-54: `static_assert(sizeof(decltype(*buffer.data())) == 1); ... return std::pair{it, end};`
> include/glaze/json/prettify.hpp:72-78, 130-136: `if (size_t(indent) >= state.size()) [[unlikely]] { state.resize(state.size()*2); if (state.size() >= max_recursive_depth_limit) [[unlikely]] { ctx.error = error_code::exceeded_max_recursive_depth; return; } }`
> include/glaze/core/context.hpp:15: `inline constexpr size_t max_recursive_depth_limit = 256;`
> include/glaze/json/prettify.hpp:178-181: `[[unlikely]] default: { ctx.error = error_code::syntax_error; return; }`
> include/glaze/json/json_format.hpp:107-154 (unpadded `read_json_string`): `for (const auto end_m7 = end - 7; it < end_m7;) { ... } // Tail end of buffer. Should be rare we even get here while (it < end) { ... } return {};` — returns empty `sv{}` on unterminated string rather than erroring.
> include/glaze/json/json_format.hpp:156-193 (`read_jsonc_comment`): identical unterminated-comment fallback returning `{}`.
> include/glaze/json/json_format.hpp:73-74: `template <auto Opts> requires(check_is_padded(Opts)) sv read_json_string(...)` is the *other*, padding-requiring overload used by the full JSON reader (`glz::read_json`), not by `prettify_json`'s default path, since `prettify_json`'s `Opts{}` doesn't set the `is_padded` internal bit.

## Other entries of this Logic Group

### glz::prettify_jsonc(const auto& in) / glz::prettify_jsonc(const auto& in, auto& out)
**Call protocol:** Same lifecycle as `prettify_json` (no init required, constructs its own `context ctx{}`, pure function). The only difference is it forces `Opts.comments = true` via `opt_true<Opts, &opts::comments>`, enabling the `Comment` branch (`read_jsonc_comment`) in the shared `detail::prettify_json` core instead of hitting the `default: syntax_error` path on `/` bytes. Same silent-error behavior as `prettify_json` (errors not surfaced to caller). Parameters: `in` must be `contiguous`; `out` (two-arg overload) must satisfy `output_buffer` (and gets `resize(in.size()*2)` then final `resize(ix)` if `resizable`). Return: `std::string` (one-arg) or void with `out` filled (two-arg). Nothing to free (RAII only).
**Evidence:**
> include/glaze/json/prettify.hpp:238-243: `template <auto Opts = opts{}> inline void prettify_jsonc(const auto& in, auto& out) { context ctx{}; detail::prettify_json<opt_true<Opts, &opts::comments>>(ctx, in, out); }`
> include/glaze/json/prettify.hpp:248-255: `template <auto Opts = opts{}> inline std::string prettify_jsonc(const auto& in) { context ctx{}; std::string out{}; detail::prettify_json<opt_true<Opts, &opts::comments>>(ctx, in, out); return out; }`
> tests/json_test/json_test.cpp:2176: `glz::prettify_jsonc(json, buffer);` called directly on a `write_jsonc`-produced string (json created at line 2174 via `glz::write_jsonc(obj, json)`).

**Combination with target in one harness:** Both `prettify_json` and `prettify_jsonc` route through the identical `detail::prettify_json<Opts>` core — the harness should call `prettify_json(in)` and `prettify_jsonc(in)` on the *same* fuzz-derived `in` buffer to exercise both the no-comment (`default: syntax_error` on stray `/`) and comment-enabled (`read_jsonc_comment`) branches without needing any special setup for either call.

### glz::minify_json(resizable auto& in) / glz::minify_json(resizable auto& in, auto& out)
**Call protocol:** `in` must be `resizable` (`concept resizable = requires(T v) { v.resize(0); }`) **and non-const** — unlike `prettify_json`'s `const auto&`, `minify_json`'s `detail::minify_json` physically calls `in.resize(in.size() + padding_bytes)` before scanning and `in.resize(in.size() - padding_bytes)` after, to guarantee padded reads for its SIMD-chunk scanner (`read_json_string`/`read_json_number`/`read_jsonc_comment`, shared with prettify). This means **the caller's buffer is mutated during the call** (temporarily grown then shrunk back) — a harness must pass an actual `std::string`/`std::vector<char>` lvalue, not a `string_view` or `const` object, or it will fail to compile/UB. No global init needed; builds its own `context ctx{}`. Return: `std::string` (one-arg) or void with `out` filled + `resize(ix)` (two-arg, if `out` is `resizable`). Nothing to free (RAII).
**Evidence:**
> include/glaze/json/minify.hpp:156-186 (`detail::minify_json`): `in.resize(in.size() + padding_bytes); ... auto [it, end] = read_iterators<Opts, true>(in); ... in.resize(in.size() - padding_bytes);`
> include/glaze/concepts/container_concepts.hpp:37: `concept resizable = requires(T v) { v.resize(0); };`
> include/glaze/json/minify.hpp:193-198: `template <auto Opts = opts{}> inline void minify_json(resizable auto& in, auto& out) { context ctx{}; detail::minify_json<Opts>(ctx, in, out); }`
> include/glaze/json/minify.hpp:200-207: `template <auto Opts = opts{}> inline std::string minify_json(resizable auto& in) { context ctx{}; std::string out{}; detail::minify_json<Opts>(ctx, in, out); return out; }`
> fuzzing/json_minify.cpp:10-21: existing project fuzzer passes a non-const `std::vector<char> buffer` lvalue to `glz::minify_json(buffer)` twice (once without, once with an appended `'\0'`), confirming the mutable-lvalue requirement and exercising both `null_terminated` branches.
> **Caveat on empty input:** `detail::minify_json` returns immediately `if (in.empty()) { return; }` (minify.hpp:160-162) *before* resizing — so an empty buffer never gets the padding treatment and `out` is left untouched (not even cleared), unlike `prettify_json`'s buffer overload which explicitly does `out.clear()` on empty input (prettify.hpp:190-192). A harness combining both should be aware `minify_json` on empty `in` leaves a stale `out` if `out` is reused across calls.

**Combination with target in one harness:** `minify_json` is the natural "consumer" of `prettify_json`'s output for round-trip validation: `prettify_json(in)` → `minify_json(prettified)` should ideally reproduce semantically-equivalent minified JSON (as in tests/json_test/json_test.cpp:2773-2776 and 2792-2796). Because `minify_json` requires a mutable resizable lvalue, the harness must copy `prettify_json`'s returned `std::string` into a fresh non-const `std::string` variable before passing it in (the return value of `prettify_json` is already such a value, so `std::string prettified = glz::prettify_json(in); glz::minify_json(prettified);` works directly as shown in the tests).

### glz::minify_jsonc(const auto& in) [two-arg] / glz::minify_jsonc(resizable auto& in) [one-arg]
**Call protocol:** Note the two overloads have **inconsistent constraints** in the current source: the two-argument overload is declared `inline void minify_jsonc(const auto& in, auto& out)` (const, non-resizable-constrained) while the one-argument overload is declared `inline std::string minify_jsonc(resizable auto& in)` (resizable-constrained, matching `minify_json`). Both ultimately call `detail::minify_json<opt_true<Opts, &opts::comments>>(ctx, in, out)`, whose body unconditionally calls `in.resize(...)` — so despite the `const auto& in` signature on the two-arg overload, `in` must still actually be a resizable, non-const container for the body to compile; the `const auto&` is effectively a template-deduction artifact, not a real const-correctness guarantee (deduction from a non-const lvalue argument still binds a mutable reference internally through the template, but if a caller passes a truly `const` object it will fail to compile when `detail::minify_json` tries to call `.resize()` on it). Project tests only exercise the one-arg form.
**Evidence:**
> include/glaze/json/minify.hpp:209-214: `template <auto Opts = opts{}> inline void minify_jsonc(const auto& in, auto& out) { context ctx{}; detail::minify_json<opt_true<Opts, &opts::comments>>(ctx, in, out); }`
> include/glaze/json/minify.hpp:216-223: `template <auto Opts = opts{}> inline std::string minify_jsonc(resizable auto& in) { context ctx{}; std::string out{}; detail::minify_json<opt_true<Opts, &opts::comments>>(ctx, in, out); return out; }`
> tests/json_test/json_test.cpp:2268: `const auto minified = glz::minify_jsonc(thing_pretty);` — `thing_pretty` is a non-const local `std::string` (mutable lvalue) passed to the one-arg overload.

**Combination with target in one harness:** `minify_jsonc` pairs with `prettify_jsonc` the same way `minify_json` pairs with `prettify_json` — `prettify_jsonc(in)` output fed into `minify_jsonc` for comment-aware round trip. A harness exercising the full Logic Group should run, on the same fuzz-derived bytes: `prettify_json(in)`, `prettify_jsonc(in)`, then take each prettified result (as a fresh mutable `std::string`) and pass it to the matching `minify_json`/`minify_jsonc`, covering both the comment-off and comment-on branches of the shared tokenizer core (`detail::prettify_json`/`detail::minify_json`) and both `null_terminated` loop variants (by also trying a non-null-terminated container such as `std::vector<char>`/`std::span<char>` without a trailing NUL, alongside a `std::string`).

## Build notes

**Claim:** Fuzz harnesses for this project are plain C++ translation units compiled with the project's own OSS-Fuzz driver script, including the library via a relative `-Iinclude` include path (header-only library, no separate link library needed beyond the fuzzing engine).
**Evidence:**
> ossfuzz_view/glaze-c5/build.sh:18: `fuzzing/ossfuzz.sh` — build.sh simply delegates to the in-repo script.
> fuzzing/ossfuzz.sh:9-16:
> ```
> $CXX --version
> for SRCFILE in $(ls fuzzing/*.cpp |grep -v -E '(exhaustive|main\.cpp)'); do
>     NAME=$(basename $SRCFILE .cpp)
>     $CXX $CXXFLAGS -std=c++23 -g -Iinclude \
>          $SRCFILE -o $OUT/$NAME \
>          $LIB_FUZZING_ENGINE
> done
> ```
> → C++23, `-Iinclude` (so harness sources `#include <glaze/glaze.hpp>` or a specific header like `#include "glaze/json/prettify.hpp"`), no extra `-l` libraries beyond `$LIB_FUZZING_ENGINE` (header-only library).
> ossfuzz_view/glaze-c5/Dockerfile:19-21: `RUN git clone --depth 1 https://github.com/stephenberry/glaze` then `WORKDIR glaze`, `COPY build.sh run_tests.sh $SRC/` — confirms the whole `glaze` checkout (including `fuzzing/` and `include/`) is what gets built in-place.
> fuzzing/json_minify.cpp:1-4: existing sibling fuzzer includes the umbrella header `#include <glaze/glaze.hpp>`, the pattern a new harness for `prettify_json` should follow.
> Entry point convention: every file in `fuzzing/*.cpp` (except `main.cpp` and anything matching `exhaustive`, excluded by the `ossfuzz.sh` glob filter) defines `extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)` (see fuzzing/json_minify.cpp:6, fuzzing/json_with_comments.cpp:15) — standard libFuzzer entry signature, each compiled to its own standalone binary named after the source file.
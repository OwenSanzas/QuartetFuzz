## target: glz::read_jmespath

### P2.0 Main path to keep (every input) — read first
**Claim:** One runtime-path call per input: `glz::jmespath_expression expression{path}` then `glz::read_jmespath<Opts>(expression, value, buffer)` where, from input control bits, `value` is `glz::generic` (default) or the reflected `Home` fixture struct, `Opts` is `glz::opts{}` or `glz::opts{.null_terminated = false}`, and `buffer` is a mutable `std::string` or a `const std::string&`; then `glz::write_json(value, out)` on success / `glz::format_error(ec, buffer)` on error.
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> include/glaze/json/jmespath.hpp:994-997: `template <auto Options = opts{}, class T, contiguous Buffer> requires(Options.format == JSON) error_ctx read_jmespath(const jmespath_expression& expression, T&& value, Buffer&& buffer)` — Opts is a template parameter of the runtime overload.
> tests/jmespath/jmespath.cpp:360-382, 429-463: `glz::opts{.null_terminated = false}` / `{.null_terminated = 0, .error_on_unknown_keys = 0}` used with `read_jmespath<options>(...)` on `std::string_view` buffers.
> include/glaze/json/jmespath.hpp:466-468, 571, 639-641: `if constexpr (not Opts.null_terminated)` end guards compiled only for null_terminated=false.
> include/glaze/json/generic_fwd.hpp:551 `using generic = generic_json<num_mode::f64>;` — destination that accepts any JSON node type.
> tests/jmespath/jmespath.cpp:11-30: Person / Family / Home fixture structs used by the project's jmespath tests.

### P2.1 Init Sequence
**Claim:** No library-wide init call is required. The only prerequisites are: (1) a JSON buffer already exists (produced e.g. by `glz::write_json` or supplied raw by the fuzzer), (2) a destination object `value` of the desired type exists (default-constructed is fine, its read is the only use), and (3) for the run-time-path overload, a `glz::jmespath_expression` must be constructed first (see that entry below) or a `std::string_view` path literal passed directly. For the compile-time overload the `Path` (a `string_literal` NTTP) is tokenized and validated at compile time via `jmespath::tokenize_as_array<S>()`, which calls `std::abort()` on a tokenization error — so a malformed compile-time path never reaches runtime, it fails to build/aborts at compile time (`include/glaze/json/jmespath.hpp:262-284`). No explicit "open" / "parser create" call exists; the function is a free template function.
**Evidence:**
> include/glaze/json/jmespath.hpp:816-818: `template <string_literal Path, auto Options = opts{}, class T, contiguous Buffer> requires(Options.format == JSON) [[nodiscard]] inline error_ctx read_jmespath(T&& value, Buffer&& buffer)`
> include/glaze/json/jmespath.hpp:821-822: `static constexpr auto tokens = jmespath::tokenize_as_array<S>(); static constexpr auto N = tokens.size();`
> include/glaze/json/jmespath.hpp:267-270 (inside `tokenize_as_array`): `if (err != tokenization_error::none) { std::abort(); }`
> tests/jmespath/jmespath.cpp:37-38: `std::string buffer{}; expect(not glz::write_json(home, buffer));` — buffer is produced before calling `read_jmespath`.

### P2.2 Parameter Construction
**Claim:** `value` (`T&&`) is any default-constructible destination type that glaze's JSON `parse<JSON>::op` can fill (string, numeric, struct, `std::vector`, etc.); it is passed by forwarding reference and mutated in place — no pre-sizing needed, `handle_slice`/`parse` clear and `emplace_back` as needed for array/slice results. `buffer` (`Buffer&&`, constrained by `contiguous` = `has_size<T> && has_data<T>`, see `include/glaze/concepts/container_concepts.hpp:66`) must be a contiguous byte range of `char`-sized elements (`static_assert(sizeof(decltype(*buffer.data())) == 1)` in `read_iterators`, `include/glaze/core/read.hpp:41`). If `Buffer` is `resizable` (has `.resize(0)`) and non-const and padding is not disabled via `Options.internal`, the target pads the buffer by `padding_bytes` (16, `include/glaze/core/opts.hpp:31`) for SWAR scanning and un-pads it afterward — so passing a `const` or non-resizable buffer (e.g. `std::string_view`, `std::span<const char>`) silently switches to the non-padded, `null_terminated`-sensitive path. `Options` is a `glz::opts{}` NTTP whose `.format` must equal `JSON` (enforced by the `requires` clause); other fields (`null_terminated`, etc.) select behavior (see P2.10).
**Evidence:**
> include/glaze/json/jmespath.hpp:816-817: template params `class T, contiguous Buffer` and `requires(Options.format == JSON)`.
> include/glaze/concepts/container_concepts.hpp:37,66,69: `concept resizable = requires(T v) { v.resize(0); };` / `concept contiguous = has_size<T> && has_data<T>;` / `concept non_const_buffer = !std::is_const_v<Buffer>;`
> include/glaze/json/jmespath.hpp:824-831: `constexpr bool use_padded = resizable<Buffer> && non_const_buffer<Buffer> && !check_disable_padding(Options); ... if constexpr (use_padded) { buffer.resize(buffer.size() + padding_bytes); }`
> include/glaze/core/read.hpp:39-54: `read_iterators` asserts `sizeof(decltype(*buffer.data())) == 1`.

### P2.3 Object Lifecycle
**Claim:** There is no persistent "object" created by `read_jmespath` itself — it is a stateless free function: tokenize path → walk buffer with local iterators → parse final value → (if padded) restore buffer size → return `error_ctx`. The only lifecycle object in the feature is `jmespath_expression` (used by the run-time-precompiled overload), whose lifecycle is: construct from a path string (runs tokenizer once) → pass by `const&` to `read_jmespath(expression, value, buffer)` one or more times → goes out of scope / destructs normally (no explicit free, it owns only `std::string_view path`, an enum, and a `std::vector<std::string_view> tokens`, all regular value members with default dtor).
**Evidence:**
> include/glaze/json/jmespath.hpp:971-991: `struct jmespath_expression { std::string_view path{}; jmespath::tokenization_error error{}; std::vector<std::string_view> tokens{}; jmespath_expression(const std::string_view input_path) noexcept : path(input_path) { error = jmespath::tokenize_full_jmespath(path, tokens); } ... };` (no custom destructor; default copy/move).
> include/glaze/json/jmespath.hpp:962-965: padding restore inside `read_jmespath`: `if constexpr (use_padded) { buffer.resize(buffer.size() - padding_bytes); }`
> tests/jmespath/jmespath.cpp:104-109: `glz::jmespath_expression expression{"family.children[0]"}; expect(not glz::read_jmespath(expression, child, buffer));` — construct once, reuse for calls.

### P2.4 Return Value Handling
**Claim:** Return type is `error_ctx` (`{size_t count; error_code ec; std::string_view custom_error_message;}`) with `operator bool()` returning true on error (`ec != error_code::none`). Callers check it with `expect(not ec)` / `if (ec)` and may format it with `glz::format_error(ec, buffer)` for diagnostics. Possible error codes surfaced by the traversal core include `syntax_error` (malformed token, invalid slice, non-array access on tuple), `unexpected_end` (non-null-terminated buffer ran out), `array_element_not_found` (index/negative index out of range, or malformed array separator), `parse_error` (bad array separator during slice fallback read), `key_not_found` (object key scan hit end without match/comma) — plus whatever `parse<JSON>::op` sets for the terminal value (e.g. type mismatch). For the run-time `jmespath_expression` overload, a tokenization failure is reported immediately as `error_code::syntax_error` with message `"JMESPath invalid expression"` without even touching the buffer.
**Evidence:**
> include/glaze/core/context.hpp:126-136: `struct error_ctx final { size_t count{}; error_code ec{}; std::string_view custom_error_message{}; operator bool() const noexcept { return ec != error_code::none; } ... };`
> include/glaze/json/jmespath.hpp:999-1001: `if (bool(expression.error)) { return {0, error_code::syntax_error, "JMESPath invalid expression"}; }`
> include/glaze/json/jmespath.hpp:967: `return {size_t(it - start), ctx.error, ctx.custom_error_message};`
> tests/jmespath/jmespath.cpp:42-44: `auto ec = glz::read_jmespath<"family.father.first_name">(first_name, buffer); expect(not ec) << glz::format_error(ec, buffer);`
> tests/jmespath/jmespath.cpp:68-69: `ec = glz::read_jmespath<"family.children[3]">(non_existent_child, buffer); expect(ec) << "Expected error for out-of-bounds index";`
> include/glaze/json/jmespath.hpp:596-599,607-609,619-621: `ctx.error = error_code::array_element_not_found;` at multiple bounds-check sites in `seek_array_index`.
> include/glaze/json/jmespath.hpp:684-685, 802-804: `ctx.error = error_code::parse_error;` on bad array-element separator.
> include/glaze/json/jmespath.hpp:952-953: `ctx.error = error_code::key_not_found;`

### P2.5 Cleanup Sequence
**Claim:** No manual cleanup of `read_jmespath`'s own state is required (no heap handles owned by the function). The one resource-shaped side effect is the temporary padding resize of a resizable, non-const `Buffer`: the target grows it by `padding_bytes` before parsing and shrinks it back by the same amount before returning — this happens on every return path (success and ctx.error) because it is unconditional code right before the `return {...}` statement (not inside an early-return branch), so no extra caller action is needed. The destination `value` and the buffer are owned by the caller and freed by their normal RAII scope exit; `jmespath_expression` likewise has only RAII members.
**Evidence:**
> include/glaze/json/jmespath.hpp:826-831 and 962-968: buffer padding grown before the traversal and `buffer.resize(buffer.size() - padding_bytes);` immediately before the single `return` at line 967 — all code paths funnel through this return (the per-token lambda `return;`s only exit the lambda / for_each iteration, not the whole function).
> include/glaze/json/jmespath.hpp:1009-1012 / (restoration for the `jmespath_expression` overload — check near its return; same `use_padded` pattern applies, confirm it also resizes back).

### P2.6 API Existence
**Claim:** All functions/types referenced below exist in the current codebase at the cited locations.
**Evidence:**
> include/glaze/json/jmespath.hpp:818: `read_jmespath(T&&, Buffer&&)` (compile-time Path overload) — exists.
> include/glaze/json/jmespath.hpp:997: `read_jmespath(const jmespath_expression&, T&&, Buffer&&)` — exists.
> include/glaze/json/jmespath.hpp:971: `struct jmespath_expression` — exists, with two constructors: `jmespath_expression(std::string_view)` (line 977) and `jmespath_expression(const char(&)[N])` (line 982).
> include/glaze/json/jmespath.hpp:216: `jmespath::tokenize_full_jmespath` — exists.
> include/glaze/json/jmespath.hpp:262: `jmespath::tokenize_as_array<S>()` — exists (compile-time only, `consteval`).
> include/glaze/json/jmespath.hpp:311: `jmespath::parse_jmespath_token` — exists.
> include/glaze/json/jmespath.hpp:568: `detail::seek_array_index` — exists.
> include/glaze/json/jmespath.hpp:441,446,627: `detail::handle_slice` (three overloads: syntax-error fallback for non-array/non-tuple T, tuple-specific, and `readable_array_t` generic) — exist.
> include/glaze/json/skip.hpp:12: `struct skip_value<JSON>` — exists, used via `skip_value<JSON>::op<Opts>(ctx, it, end)`.
> include/glaze/json/read.hpp:45: `struct parse<JSON>` — exists, used via `parse<Opts.format>::template op<Opts>(value, ctx, it, end)`.
> **Important caveat:** There is no stand-alone runtime-path overload that takes a plain `std::string_view path` directly in `jmespath.hpp` itself as a distinct function signature — the tests call `glz::read_jmespath("family.father.first_name", first_name, buffer)` (tests/jmespath/jmespath.cpp:74), but this resolves via **implicit construction of `jmespath_expression`** from the string literal (constructor at jmespath.hpp:982-986, `jmespath_expression(const char (&input_path)[N])`) binding to the `const jmespath_expression&` parameter of the overload at line 997 — it is not a separate overload, it is the same function at line 997 invoked through implicit conversion. Confirm: no other `read_jmespath` overload exists in the file (grep found only lines 818 and 997).

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists. The only "paired" construct is `jmespath_expression{path}` construction (which runs tokenization once, paired implicitly with — not followed by any explicit "close") then one or more `read_jmespath(expression, value, buffer)` calls that each re-check `expression.error` before using `expression.tokens`. `write_json` → `read_jmespath` is the natural round-trip pairing seen in every test (write a JSON buffer, then query it), but `write_json` is not required — raw/fuzzer-provided JSON bytes work equally as `buffer`.
**Evidence:**
> tests/jmespath/jmespath.cpp:37-38, 42: `std::string buffer{}; expect(not glz::write_json(home, buffer)); ... glz::read_jmespath<"family.father.first_name">(first_name, buffer);`
> tests/jmespath/jmespath.cpp:107-108: `glz::jmespath_expression expression{"family.children[0]"}; expect(not glz::read_jmespath(expression, child, buffer));`
> include/glaze/json/jmespath.hpp:999-1001: every call of the `jmespath_expression` overload re-validates `expression.error` before touching `buffer`.

### P2.8 Prerequisite State
**Claim:** No global/registry/connection state is required; `read_jmespath` is a pure, free, templated function operating only on its arguments. The only "prerequisite state" is the content of `buffer` being valid enough to be interpreted as bytes (it need not be well-formed JSON overall — only the portion the path walks through needs to parse; skip_value/skip_string_view simply scan past the rest). `Options.null_terminated` is a prerequisite contract: if true (the default when the buffer is not auto-padded, see P2.10) the scanners assume a `'\0'` sentinel exists just past `end` and skip bounds checks (`at_end()` lambdas compile out when `Opts.null_terminated` is true, e.g. jmespath.hpp:468-475, 570-578, 638-646) — passing a non-null-terminated buffer with `null_terminated=true` is undefined/out-of-bounds-read-prone, which the code comments explicitly flag as the cause of a past bug (#2710).
**Evidence:**
> include/glaze/json/jmespath.hpp:432-437: "`null_terminated` reverted to true, so the end guards in `handle_slice` compiled out while the caller was parsing a buffer with no '\\0' sentinel" (explaining issue #2710).
> include/glaze/json/jmespath.hpp:467-475: `if constexpr (not Opts.null_terminated) { if (it == end) [[unlikely]] { ctx.error = error_code::unexpected_end; return true; } } return false;`
> include/glaze/core/opts.hpp (referenced): `Opts.null_terminated` flag controlling SWAR/bounds-check compilation.

### P2.9 Result Usage
**Claim:** The filled-in `value` object is the primary result; real callers (tests) immediately read its fields/compare equality (`expect(child.first_name == "Lilly")`) or pass it to `glz::format_error`/further glaze APIs like `glz::write_json` for round-tripping/printing. The `error_ctx` return is consumed by `glz::format_error(ec, buffer)` to build a human-readable diagnostic string, and by boolean checks (`if (ec)`/`expect(not ec)`) gating whether `value` is trustworthy. No separate "close"/"commit" API consumes the result; `value`'s type determines what further APIs apply to it (e.g. if `value` is a `std::vector<int>`, callers index/iterate it directly, as in the slice tests).
**Evidence:**
> tests/jmespath/jmespath.cpp:42-44: `auto ec = glz::read_jmespath<...>(first_name, buffer); expect(not ec) << glz::format_error(ec, buffer); expect(first_name == "Gilbert");`
> tests/jmespath/jmespath.cpp:144-150: `expect(not glz::read_jmespath<"[0:5]">(slice, buffer)); expect(slice.size() == 5); expect(slice[0] == 0); ...` — direct indexing of the result container.
> tests/jmespath/jmespath.cpp:178-179: `expect(not glz::read_jmespath(expr, slice, buffer)); expect(slice == (std::vector<int>{5, 4, 3, 2}));` — equality-compares the filled container.
> include/glaze/core/reflect.hpp:3348-3359: `format_error(const error_ctx& pe, const auto& buffer)` consumes `pe.count`/`pe.ec`/`pe.custom_error_message` together with the original `buffer` to build the message — this is the main "downstream" consumer of the `error_ctx` plus the original buffer.

### P2.10 Behaviour Controls
**Claim:** Several axes select different processing paths:
1. **Path form** — compile-time `string_literal Path` (template, validated/aborted at compile time) vs. run-time `jmespath_expression` (validated at construction, error checked at call time) — different overloads (`read_jmespath<Path,...>` at line 818 vs `read_jmespath(const jmespath_expression&, ...)` at line 997), same core traversal logic duplicated between them (`for_each<N>` compile-time unrolled loop vs. a run-time `for` loop).
2. **Token kind** per path segment, decided by `jmespath::parse_jmespath_token`: plain key (`is_array_access=false`) → object-member scan loop (jmespath.hpp:906-957/road through key compare); array-access with `colon_count==0` → single-index path via `detail::seek_array_index` (supports negative indices, scans to count elements first — jmespath.hpp:580-610); array-access with `colon_count==1|2` → slice path via `detail::handle_slice`, which itself branches: `step==0` → `syntax_error` (jmespath.hpp:654-657/qualifies both overload variants); `step==1 && no negative indices` → an optimized single-pass read/skip (jmespath.hpp:753-811); otherwise (negative step or negative indices) → "read whole array then slice in place" fallback (jmespath.hpp:660-751).
3. **`T` category selects which `handle_slice` overload participates**: `readable_array_t<T>` (vectors etc., full slice support) vs. tuple types (`tuple_t`/`is_std_tuple`, only `step==1` and non-negative indices supported, else `syntax_error`) vs. anything else (`syntax_error` unconditionally) — jmespath.hpp:438-444, 446-463, 627-630.
4. **`Options.null_terminated`** (and the derived `Opts` after `is_padded_on/is_padded_off`) toggles whether `at_end()` bounds checks are compiled in at all — directly affects whether a non-null-terminated buffer is read safely or can run past `end`.
5. **Buffer "paddability"** (`resizable<Buffer> && non_const_buffer<Buffer> && !check_disable_padding(Options)`) decides `use_padded`, which (a) temporarily grows/shrinks the buffer by `padding_bytes=16` for SWAR scanning and (b) forces `is_padded_on<Options>()`/`is_padded_off<Options>()`, changing the effective `Opts` seen by every downstream `skip_value`/`parse` call.
**Evidence:**
> include/glaze/json/jmespath.hpp:310-427: `parse_jmespath_token` branches on `colon_count` (0 / 1-2 / >2 invalid) and sets `is_array_access`.
> include/glaze/json/jmespath.hpp:654-657: `if (step_idx == 0) [[unlikely]] { ctx.error = error_code::syntax_error; return; }`
> include/glaze/json/jmespath.hpp:660, 716, 753: three-way branch in the generic `handle_slice` (`step!=1 or negative` fallback vs `step==1` compaction vs `step==1` optimized partial read).
> include/glaze/json/jmespath.hpp:438-444 and 446-463: `requires(... not readable_array_t<T> ...)` → `syntax_error`; `requires(... tuple_t<...> ...)` → restricted slice.
> include/glaze/json/jmespath.hpp:824-826: `use_padded` / `Opts` derivation shown above (P2.2).
> tests/jmespath/jmespath.cpp:261-271,273-287: step==0 and int32-overflowing literal tests exercising these controls and expecting `ec` truthy (error).

6. **Destination `glz::generic`** (previously missing): unlike a fixed T it parses whatever node the path locates (object/array/string/number/bool/null) through the full JSON reader; it is the most permissive destination and should be the default.
7. **`glz::opts{.null_terminated = false}`** must actually be passed (template parameter of both overloads) on a share of inputs; without it the null_terminated=false code is never compiled into the harness.

### P2.11 Input Expectations
**Claim:** The **path** input (either the `Path` NTTP string or the run-time string passed to `jmespath_expression`) is expected to be a dotted/bracketed JMESPath-like expression over printable text: delimiters `.`, `[`, `]`; quoting with `'` or `"` for keys that need escaping; bracket content is either a bare integer (single index, parsed via `std::from_chars` into a 32-bit `int`, `include/glaze/json/jmespath.hpp:297-308`, so `"[9999999999]"` fails to parse → `result.error` → `syntax_error`) or 0-2 colons of optionally-signed integers for a slice; malformed bracket/paren/quote nesting is caught by `tokenize_jmes_path`/`tokenize_full_jmespath` returning `tokenization_error::{unbalanced_brackets, unbalanced_parentheses, unclosed_string, invalid_escape_sequence, unexpected_delimiter}` (jmespath.hpp:20-27, 152-165); a consecutive-delimiter/empty-token case (e.g. `"family..father"`) yields `unexpected_delimiter` at jmespath.hpp:228-230. The **JSON buffer** is expected to be valid-enough JSON along the path being walked (it need not be exhaustively validated outside the queried subtree, since `skip_value`/`skip_string_view` simply skip bytes without fully re-verifying every skipped value's grammar beyond what `skip_value<JSON>::op` itself checks). Buffer termination: if `Opts.null_terminated` is true, the scanners assume a sentinel `'\0'` exists at/after `end` and do not re-check `it != end` before dereferencing (`at_end()` compiles to `return false;` unconditionally, jmespath.hpp:467-475, 570-578, 638-646); if false, every structural dereference is preceded by an explicit `it == end` check that sets `ctx.error = error_code::unexpected_end`. Buffer size has no hard documented minimum/maximum beyond what `resize()`/padding can accommodate; `padding_bytes = 16` extra bytes are appended for SWAR when the buffer is resizable/non-const and padding isn't disabled (jmespath.hpp:824-831, opts.hpp:31).
**Evidence:**
> include/glaze/json/jmespath.hpp:297-308: `parse_int` uses `std::from_chars` into `int` — rejects out-of-range literals, matching test `tests/jmespath/jmespath.cpp:280-287` (`"[9999999999]"` etc. all expected to error).
> include/glaze/json/jmespath.hpp:20-27: `enum struct tokenization_error { none, unbalanced_brackets, unbalanced_parentheses, unclosed_string, invalid_escape_sequence, unexpected_delimiter };`
> include/glaze/json/jmespath.hpp:228-230: `if (result.first.empty()) { return tokenization_error::unexpected_delimiter; }`
> tests/jmespath/jmespath.cpp:131-133: `ec = glz::read_jmespath("family..father", invalid_query_result, buffer); // Invalid due to double dot` → `expect(ec)`.
> include/glaze/json/jmespath.hpp:432-437 and 467-475: null_terminated contract and compiled-out guard explained in the comment about issue #2710.

## Other Logic-Group Entries

### glz::jmespath_expression (constructors)
**Claim:** This is a plain value type, not a handle needing explicit destruction. Two constructors exist: `jmespath_expression(std::string_view)` (jmespath.hpp:977-980) and `jmespath_expression(const char (&)[N])` (jmespath.hpp:982-986); both immediately call `jmespath::tokenize_full_jmespath(path, tokens)` and store the resulting `tokenization_error` in the public `error` member plus the token list in `tokens`. A harness should: construct it from a (possibly fuzzer-controlled) path string, check `.error` itself (to separately log/classify tokenization failures distinct from traversal failures — exactly the scenario the "Functionality" description calls out), then pass it by const-ref to `read_jmespath(expression, value, buffer)`. It can be constructed once and reused across multiple `read_jmespath` calls (same tokens reused; see tests/jmespath/jmespath.cpp:104-109). It holds a `std::string_view path` — the caller-owned string must outlive the `jmespath_expression` and every `read_jmespath` call made with it, since `tokens` are `string_view`s into `path`.
**Evidence:**
> include/glaze/json/jmespath.hpp:971-991: full struct definition (fields `path`, `error`, `tokens`; constructors call `tokenize_full_jmespath`; default copy/move, no custom destructor).
> include/glaze/json/jmespath.hpp:999-1001: `read_jmespath` checks `expression.error` first and bails out with a `syntax_error` `error_ctx` if set, without touching `buffer` or `value`.
> tests/jmespath/jmespath.cpp:177-178: `glz::jmespath_expression expr{"[10:0:-1]"}; expect(not glz::read_jmespath(expr, slice, buffer));` — construct-then-use pattern, `expr` is a local `std::string_view`-backed literal (lifetime of the string literal is static, safe).
> tests/jmespath/jmespath.cpp:268-270: `expect(glz::read_jmespath(glz::jmespath_expression{"[5:0:0]"}, out, buffer));` — temporary `jmespath_expression` constructed inline, used immediately within the same full-expression (safe since the backing string literal is static storage); **caution for harness authors using a fuzzer-owned `std::string` for the path**: the `jmespath_expression(std::string_view)` ctor only stores a view, so the backing `std::string` must stay alive across the `read_jmespath` call.

### glz::jmespath::tokenize_full_jmespath (Core, used by jmespath_expression and the run-time read_jmespath path)
**Claim:** Pure function `tokenization_error tokenize_full_jmespath(std::string_view expression, std::vector<std::string_view>& tokens)` (jmespath.hpp:216-217). It clears `tokens`, repeatedly calls `tokenize_jmes_path` to split off the next top-level token (splitting on bracket/paren/quote-aware top-level delimiters), and finally runs `finalize_tokens` to split each key token further at `[`/`]` boundaries (jmespath.hpp:168-206). Returns `tokenization_error::none` on success; any non-`none` value means `tokens` is not usable (callers must check this return/the `jmespath_expression::error` mirror of it before indexing `tokens`). Not callable from a harness in isolation in a meaningful way beyond what `jmespath_expression` already exposes — it is Core, reached only through `jmespath_expression`'s constructors or the compile-time `tokenize_as_array`.
**Evidence:**
> include/glaze/json/jmespath.hpp:216-217: signature.
> include/glaze/json/jmespath.hpp:219, 224-230, 243ff: clears tokens, loops `tokenize_jmes_path`, propagates errors, then (implied by comments) calls `finalize_tokens`.
> include/glaze/json/jmespath.hpp:168-206: `finalize_tokens` further splits on `[`...`]`, returning `unbalanced_brackets` on a missing `]`.
> include/glaze/json/jmespath.hpp:979, 985: both `jmespath_expression` constructors call this directly.

### glz::jmespath::tokenize_as_array (Core, compile-time only)
**Claim:** `consteval auto tokenize_as_array<const std::string_view& expression>()` (jmespath.hpp:261-284) is purely a compile-time facility used internally by the `Path`-NTTP overload of `read_jmespath` (jmespath.hpp:820-821). It calls `tokenize_full_jmespath` twice (once to size an array, once to fill it) and `std::abort()`s on any tokenization error — meaning a malformed compile-time path is a hard compile-time failure (or a `consteval`-evaluation failure), not a run-time error a harness can catch. Not independently fuzzable since its argument must be a `static constexpr`/NTTP-bound string; a harness can only exercise it indirectly by choosing different literal `Path` values at the call sites it writes (which must all be known at compile time).
**Evidence:**
> include/glaze/json/jmespath.hpp:261-284: full definition, `std::abort()` on tokenization error at line 268/276.
> include/glaze/json/jmespath.hpp:820-822: only call site, inside `read_jmespath<Path,...>`.

### glz::jmespath::parse_jmespath_token (Core)
**Claim:** `constexpr ArrayParseResult parse_jmespath_token(std::string_view token)` (jmespath.hpp:311-427) decomposes one already-split token into `{is_array_access, error, key, start, end, step, colon_count}`. Called from both `read_jmespath` overloads, once per token, to decide which of the per-token code paths (object-key scan / single index / slice) to take (jmespath.hpp:852, 1034). It never touches the JSON buffer or `ctx` — it is pure string parsing of the path only — so it cannot itself produce `ctx.error`; its `result.error` is what the callers translate into `ctx.error = error_code::syntax_error`.
**Evidence:**
> include/glaze/json/jmespath.hpp:311-427: definition; uses `parse_int`/`std::from_chars` for bound literals (int32 range only).
> include/glaze/json/jmespath.hpp:852 and 1034: both call sites, immediately followed by `static_assert(not decomposed_key.error, ...)` (compile-time path) or `if (decomposed_key.error) { ctx.error = error_code::syntax_error; return; }` (run-time path, jmespath.hpp:1039-1042).

### glz::detail::seek_array_index (Core)
**Claim:** `void seek_array_index<Opts>(int32_t n, context& ctx, auto&& it, auto end)` (jmespath.hpp:567-625) advances `it` from just-past-`[` to the start of array element `n`. Negative `n` requires a full forward scan first to count elements (`skip_value<JSON>::op<Opts>` repeatedly) before rewinding and re-skipping to the resolved positive index — this is the security-relevant double-scan the feature description calls out. Sets `ctx.error = error_code::array_element_not_found` on any separator mismatch, out-of-range negative index, or on malformed array syntax encountered via `skip_value`. Only called from within `read_jmespath`'s two bodies (jmespath.hpp:887, 1072) for the "single index" branch; never called standalone by any test.
**Evidence:**
> include/glaze/json/jmespath.hpp:567-625: full definition, including the negative-index count-then-rewind logic (580-610) and the forward-skip loop (612-624).
> include/glaze/json/jmespath.hpp:887 and 1072: the two call sites inside `read_jmespath`.

### glz::detail::handle_slice (Core, 3 overloads)
**Claim:** Overload set resolved by `T`'s category via `requires` clauses: (1) jmespath.hpp:438-444 — any `T` that is neither `readable_array_t` nor a tuple: unconditionally `ctx.error = error_code::syntax_error` (slicing not supported for that destination type); (2) jmespath.hpp:446-521ish — tuple types: only `step==1` and non-negative bounds supported, else `syntax_error`; (3) jmespath.hpp:627-812 — `readable_array_t<T>` (e.g. `std::vector`): full slice support with the three sub-paths described in P2.10. Comment at jmespath.hpp:432-437 stresses `Opts` must be passed explicitly (not defaulted) by every caller because a missing `<Opts>` would silently reset to `opts{}` and compile out the non-null-terminated bounds guards — a documented historical bug (#2710). Called only from inside `read_jmespath`'s two bodies (jmespath.hpp:879, 1063).
**Evidence:**
> include/glaze/json/jmespath.hpp:430-444, 446-521(approx), 627-812: three `requires`-disambiguated overloads.
> include/glaze/json/jmespath.hpp:432-437: comment on why `Opts` has no default here.
> include/glaze/json/jmespath.hpp:879 and 1063: call sites.

### glz::detail::skip_value<JSON>::op and glz::detail::parse<JSON>::op (Core, shared with the rest of glaze's JSON reader)
**Claim:** These are glaze's general JSON structural skipper and value parser, not jmespath-specific; `read_jmespath` reuses them to skip uninteresting array elements/object members (`skip_value<JSON>::op<Opts>(ctx, it, end)`) and to materialize the finally-located node into `value` (`parse<Opts.format>::template op<Opts>(value, ctx, it, end)`). They exist and are declared/defined at `include/glaze/json/skip.hpp:12` and `include/glaze/json/read.hpp:45` respectively (confirmed via grep), and are used throughout glaze's own `glz::read_json`/`glz::read` machinery, not introduced by jmespath.hpp. A harness does not call these directly; they are reached only through `read_jmespath`.
**Evidence:**
> include/glaze/json/skip.hpp:12: `struct skip_value<JSON>` — definition site.
> include/glaze/json/read.hpp:45: `struct parse<JSON>` — definition site.
> include/glaze/json/jmespath.hpp:589,613,671(ish),776,788,944: multiple `skip_value<JSON>::op<Opts>(...)` call sites inside jmespath Core helpers.
> include/glaze/json/jmespath.hpp:671,782,840,894,939,1021: multiple `parse<Opts.format>::template op<Opts>(...)` call sites, including the `N==0` fallback (whole-buffer parse, no path) at lines 840/1021.

## Build notes
**Claim:** The project's OSS-Fuzz build compiles every `fuzzing/*.cpp` file (except files matching `exhaustive` or `main.cpp`) as a standalone libFuzzer-style target in C++23, with only `-Iinclude` as the include path (glaze is a header-only library, so `#include <glaze/json/jmespath.hpp>` or `#include <glaze/glaze.hpp>` resolves against `include/glaze/...` directly) and links `$LIB_FUZZING_ENGINE`. The repo's existing `fuzzing/json_jmespath.cpp` is currently a stub (`LLVMFuzzerTestOneInput` does nothing) and does not exercise `read_jmespath` at all — the downstream harness generator must write real logic there. Real usage patterns (how to call the target, what headers/macros to use) are best taken from `tests/jmespath/jmespath.cpp`, which includes `"glaze/json/jmespath.hpp"` plus `"glaze/glaze.hpp"` for `glz::write_json`/`glz::format_error`; a fuzz harness only strictly needs `glaze/json/jmespath.hpp` (it pulls in `glaze/json/read.hpp`, `glaze/json/skip.hpp`, `glaze/core/seek.hpp`, `glaze/util/parse.hpp`, `glaze/util/string_literal.hpp` itself, per its own includes).
**Evidence:**
> ${QF_WORK}/ossfuzz_view/glaze-c3/build.sh:18: `fuzzing/ossfuzz.sh`
> fuzzing/ossfuzz.sh:11-16: `for SRCFILE in $(ls fuzzing/*.cpp |grep -v -E '(exhaustive|main\.cpp)'); do NAME=$(basename $SRCFILE .cpp); $CXX $CXXFLAGS -std=c++23 -g -Iinclude $SRCFILE -o $OUT/$NAME $LIB_FUZZING_ENGINE; done`
> fuzzing/json_jmespath.cpp:1-13: current stub harness — `LLVMFuzzerTestOneInput` is a no-op, confirming no existing real jmespath fuzz logic to copy from.
> fuzzing/CMakeLists.txt:56: `create_fuzztest(json_jmespath)` — local/non-ossfuzz CMake build also wires up this target name, confirming the expected harness file name convention.
> include/glaze/json/jmespath.hpp:1-14: `#include "glaze/core/seek.hpp"`, `"glaze/json/read.hpp"`, `"glaze/json/skip.hpp"`, `"glaze/util/parse.hpp"`, `"glaze/util/string_literal.hpp"` — header-only, no separate library to link beyond the fuzzing engine.
> tests/jmespath/jmespath.cpp:4,6: `#include "glaze/json/jmespath.hpp"` and `#include "glaze/glaze.hpp"` — real usage reference for a harness author.
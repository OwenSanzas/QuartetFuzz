## target: glz::read_beve

### P2.1 Init Sequence
**Claim:** No library-level global init is required. `read_beve` is a free function template; the only "setup" needed is constructing the destination value `T` (default- or partially-constructed, by reference) and a contiguous byte buffer holding the BEVE bytes. Internally it default-constructs a fresh `glz::context` per call (via the 1-arg overload of `glz::read`), so no context/handle must be created by the caller.
**Evidence:**
> include/glaze/beve/read.hpp:3190-3194:
> ```
> template <read_supported<BEVE> T, class Buffer>
> [[nodiscard]] inline error_ctx read_beve(T&& value, Buffer&& buffer)
> {
>    return read<opts{.format = BEVE}>(value, std::forward<Buffer>(buffer));
> }
> ```
> include/glaze/core/read.hpp:216-222: the `read<Opts>(T&, Buffer&&)` overload that `read_beve` resolves to constructs `format_context_t<Opts.format> ctx{};` itself and forwards to the context-taking overload — callers never need to build a context for ordinary `read_beve` use.
> include/glaze/core/opts.hpp:1580: `concept read_supported = requires { from<Format, std::remove_cvref_t<T>>{}; };` — the only compile-time prerequisite is that `T` have a `from<BEVE,T>` specialization (i.e. is reflectable/has `glz::meta`, or is a container/primitive Glaze already knows how to parse).

### P2.2 Parameter Construction
**Claim:** `value` is any mutable object of a BEVE-readable type `T` (struct/class with glz::meta or reflectable members, STL container, map, variant, optional, tuple, numeric/typed array, string, enum, filesystem path, etc.), passed by forwarding reference and mutated in place. `buffer` must be a *contiguous* byte range (`sizeof(*buffer.data())==1` is `static_assert`'d deeper in the call chain) such as `std::string`, `std::vector<char>`/`<uint8_t>`, or `std::string_view`/span; non-contiguous or streaming buffers are rejected by concept at compile time.
**Evidence:**
> include/glaze/core/read.hpp:133-137: `template <auto Opts, class T, contiguous Buf> requires read_supported<T, Opts.format> && (!is_input_streaming<Buf>) [[nodiscard]] error_ctx read(T& value, Buf&& buffer, is_context auto&& ctx)` — requires `contiguous` concept (`include/glaze/concepts/container_concepts.hpp:66: concept contiguous = has_size<T> && has_data<T>;`).
> include/glaze/core/read.hpp:137: `static_assert(sizeof(decltype(*buffer.data())) == 1);` inside the `read` body — enforces byte-sized elements.
> include/glaze/beve/read.hpp:3287 (read_beve_delimited): `static_assert(sizeof(decltype(*buffer.data())) == 1);` — same byte-buffer constraint repeated for the delimited variant.
> include/glaze/core/read.hpp:141-144: `if (buffer.empty()) { ctx.error = error_code::no_read_input; return {0, ctx.error, ...}; }` — an empty buffer is explicitly rejected (except NDJSON format).

### P2.3 Object Lifecycle
**Claim:** There is no persistent "object" created by `read_beve` itself (no opaque handle to destroy). The lifecycle is: (1) caller constructs/owns `value` (often default-constructed) and `buffer` (filled with raw bytes, e.g. from fuzzer input or from a prior `write_beve`); (2) call `read_beve(value, buffer)`; (3) inspect the returned `error_ctx` for success/failure; (4) on success use `value` normally (it now holds the decoded data) — no explicit free step for `value` or the internal `context` (both are stack-based, destroyed automatically at scope exit).
**Evidence:**
> include/glaze/beve/read.hpp:3191-3194 (shown above): the function takes `value` by forwarding reference and returns only an `error_ctx`; it owns no heap resource that must be freed by the caller.
> include/glaze/core/context.hpp:141-154: `struct context { error_code error{}; ...; uint32_t depth{}; std::string current_file; std::string scratch{}; };` — constructed on the stack per-call by `read<Opts>(T&,Buffer&&)` (read.hpp:220), no explicit destroy needed.

### P2.4 Return Value Handling
**Claim:** `read_beve` returns `glz::error_ctx{ size_t count; error_code ec; std::string_view custom_error_message; }`. Callers must check `bool(result)` (true means *error*, matching `std::error_code` semantics) or compare `result == glz::error_code::some_code`/`glz::error_code::none`. `count` is the number of bytes consumed, usable to detect partial-reads. The `expected<T,error_ctx>` overload instead returns a `std::expected`; failure is an `unexpected(error_ctx)`.
**Evidence:**
> include/glaze/core/context.hpp:126-136: `struct error_ctx final { size_t count{}; error_code ec{}; std::string_view custom_error_message{}; operator bool() const noexcept { return ec != error_code::none; } bool operator==(const error_code e) const noexcept { return ec == e; } };`
> tests/beve_test/beve_test.cpp:102: `expect(!glz::read_beve(dst, buffer));` — `!` on `error_ctx` means "no error" (operator bool is true-on-error).
> tests/beve_test/beve_test.cpp:1566: `expect(glz::read_file_beve(obj, "../alabastar.beve", std::string{}) == glz::error_code::none);` — comparison against an explicit error_code.
> include/glaze/beve/read.hpp:3196-3205: the value-returning overload: `const auto pe = read<...>(value, ...); if (pe) { return unexpected(pe); } return value;` — `pe` truthy (error) short-circuits to `unexpected`.

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup/free calls are required by `read_beve` itself: `value` and the internal `context` are value-typed/stack objects destroyed by normal C++ scope rules; `buffer` is owned by the caller (e.g. `std::string`/`std::vector`) and destroyed normally. If `use_padded` padding was applied to a resizable buffer, the library itself restores the buffer to its original size before returning (`buffer.resize(original_size)`), so the caller does not need to undo that.
**Evidence:**
> include/glaze/core/read.hpp:147-154, 208-211:
> ```
> constexpr bool use_padded = resizable<Buffer> && non_const_buffer<Buffer> && !check_disable_padding(Opts);
> ...
> if constexpr (use_padded) { original_size = buffer.size(); buffer.resize(original_size + padding_bytes); }
> ...
> finish:
>    ...
>    if constexpr (use_padded) { buffer.resize(original_size); }  // Restore the original buffer state
> ```
> This shows the library mutates and then restores the caller's buffer transparently — no caller-side cleanup action needed.

### P2.6 API Existence
**Claim:** All functions named in this report exist in the current codebase at the cited locations (verified via Read/Grep, not assumed).
**Evidence:**
> include/glaze/beve/read.hpp:3191 `read_beve(T&&, Buffer&&)` (target)
> include/glaze/beve/read.hpp:3197 `read_beve(Buffer&&) -> expected<T,error_ctx>`
> include/glaze/beve/read.hpp:3208 `read_file_beve(T&, sv file_name, auto&& buffer)`
> include/glaze/beve/read.hpp:3245 `read_beve_untagged(T&&, Buffer&&)`
> include/glaze/beve/read.hpp:3252 `read_beve_untagged(Buffer&&) -> expected<T,error_ctx>`
> include/glaze/beve/read.hpp:3264 `read_file_beve_untagged(T&, const std::string&, auto&&)`
> include/glaze/beve/read.hpp:3285 `read_beve_delimited(Container&, Buffer&&)`
> include/glaze/beve/read.hpp:3346 `read_beve_delimited<Container>(Buffer&&) -> expected<Container,error_ctx>`
> include/glaze/beve/write.hpp:1837 `write_beve(T&&, Buffer&&)`
> include/glaze/beve/write.hpp:1843 `write_beve(T&&) -> expected<std::string,error_ctx>`
> include/glaze/beve/write.hpp:1855 `write_file_beve(T&&, sv file_name, auto&& buffer)`
> include/glaze/core/read.hpp:135,218 `read<Opts>(T&, Buffer&&, ctx)` / `read<Opts>(T&, Buffer&&)` (core entry both read_beve overloads funnel into)
> include/glaze/beve/header.hpp:232 `int_from_compressed(ctx, it, end)`
> include/glaze/core/context.hpp:173 `struct depth_guard`
> include/glaze/beve/read.hpp:2973 `decode_hash_with_size<BEVE,T,HashInfo,...>::op(it,end,n)` call site (declared elsewhere, e.g. glaze/core hash utilities)
> include/glaze/beve/read.hpp:32 `struct parse<BEVE>` with `op(...)`
> (skip_value<BEVE>::op is referenced throughout read.hpp, e.g. lines 44, 64, 169, 730, 936, 993, 1221, 1232, 1260, 2965 — defined in include/glaze/beve/skip.hpp, included at read.hpp:8.)

### P2.7 Co-call Constraints
**Claim:** `read_beve` has no mandatory "paired" open/close API of its own (it's a single stateless call). However: (a) `read_file_beve` pairs naturally with `write_file_beve` for round-trip file tests (not a strict protocol requirement, just how tests use it); (b) `read_beve_delimited` must be paired, semantically, with buffers produced by `write_beve_delimited`/`write_beve_append_with_delimiter` if one wants well-formed multi-value streams, though it also just works on any buffer the fuzzer provides; (c) within `depth_guard`, every `guard{ctx}`/entry that returns `entered==true` increments `ctx.depth` and is automatically paired with a decrement in its destructor — this is an internal RAII invariant, not something the harness manages directly.
**Evidence:**
> include/glaze/core/context.hpp:178-190: `depth_guard(Ctx& c) { if (ctx.depth >= max_recursive_depth_limit) { ctx.error = ...; return; } ++ctx.depth; entered = true; } ~depth_guard() { if (entered) --ctx.depth; }` — paired increment/decrement, RAII, no harness action needed.
> tests/beve_test/beve_test.cpp:1560,1566: `glz::write_file_beve(obj, "../alabastar.beve", ...)` then `glz::read_file_beve(obj, "../alabastar.beve", ...)` — illustrative pairing in tests, not enforced by the API.
> tests/beve_test/beve_test.cpp:5395,5401 and 5418,5423: `glz::write_beve_delimited(values, buffer); ... glz::read_beve_delimited(result, buffer);` — round-trip pairing pattern used by tests.

### P2.8 Prerequisite State
**Claim:** No external/global state (no network connection, no registry, no prior "open" call) must exist before calling `read_beve`. The only prerequisite is: `T` must satisfy `read_supported<T, BEVE>` at compile time (i.e. have a `glz::from<BEVE,T>` specialization, generally automatic via reflection/meta), and the buffer must be a non-streaming contiguous byte range. For `read_file_beve`, the prerequisite is that `ctx.current_file`/file path be valid and the underlying file be readable (handled internally by `file_to_buffer`).
**Evidence:**
> include/glaze/core/opts.hpp:1580: `concept read_supported = requires { from<Format, std::remove_cvref_t<T>>{}; };`
> include/glaze/beve/read.hpp:3208-3220 (read_file_beve): `ctx.current_file = file_name; const auto file_error = file_to_buffer(buffer, ctx.current_file); if (bool(file_error)) { return error_ctx{0, file_error}; } return read<set_beve<Opts>()>(value, buffer, ctx);` — file I/O failure is surfaced as an error_ctx before any BEVE parsing starts.

### P2.9 Result Usage
**Claim:** After a successful `read_beve`, callers use `value` directly as a normal C++ object: comparing it (`dst == src`), feeding it into other Glaze converters (e.g. `glz::beve_to_json`), re-serializing with `write_beve`/`write_json`, or passing to arbitrary application logic. The `error_ctx` result is consumed only for its boolean/error-code/count fields (see P2.4), not passed into further Glaze API calls.
**Evidence:**
> tests/beve_test/beve_test.cpp:101-107 (verify_map_roundtrip):
> ```
> expect(!glz::read_beve(dst, buffer));
> expect(dst == src);
> std::string json{};
> expect(!glz::beve_to_json(buffer, json));
> expect(json == R"({"42":"life","9001":"power"})");
> ```
> — shows the decoded `dst` being compared, and the *raw buffer* (not `dst`) being fed into `glz::beve_to_json` (a sibling BEVE→JSON converter operating on the same bytes, independent code path).
> tests/beve_test/beve_test.cpp:122-131 (verify_unordered_map_roundtrip): decoded `dst` compared to `src`; `buffer` also passed to `glz::beve_to_json`, whose JSON output is then read back with `glz::read_json` — demonstrates results chained into other format converters.
> tests/beve_test/beve_test.cpp:1566-1570 (file_include_test): after `glz::read_file_beve(obj, ...)`, fields of `obj` (`obj.str`, `obj.i`, `obj.j`) are asserted directly.

### P2.10 Behaviour Controls
**Claim:** The primary behaviour-selecting parameter is the `Opts` template value (`glz::opts{.format=BEVE, ...}`), set by each entry point:
- `read_beve`/`read_beve(Buffer&&)`: fixed `opts{.format = BEVE}` — default `structs_as_arrays=false` ⇒ keyed/hashed object reader (`decode_hash_with_size`-based) for structs.
- `read_beve_untagged`/`read_binary_untagged` (deprecated alias): `opt_true<opts{.format=BEVE}, structs_as_arrays_opt_tag{}>` ⇒ `structs_as_arrays=true`, forcing the positional tuple/array reader path instead of key matching — a genuinely different code path through `from<BEVE,T>`.
- `read_file_beve`/`read_file_beve_untagged<Opts>`: take a user-suppliable `Opts` template parameter (default `opts{}`), then force `.format=BEVE` via `set_beve<Opts>()`; `read_file_beve_untagged` additionally forces `structs_as_arrays=true` via `opt_true<Opts, structs_as_arrays_opt_tag{}>`.
- `read_beve_delimited<Opts=opts{}, Container>`: same `Opts` forced to BEVE via `set_beve<Opts>()`, applied once per element in a loop; additionally reads/validates `tag::delimiter` bytes between elements.
- `opts.error_on_unknown_keys` (default true) and `opts.error_on_missing_keys` (default false) and `opts.partial_read` (default false) further gate the keyed-object reader's behaviour on unexpected/missing fields (see read.hpp:2928,2942,2976 and 1224-1231 for the variant adjacent-tag reader).
- The wire bytes themselves select processing paths at runtime: the first tag byte of each value dispatches through `from<BEVE,T>::op` to struct/object, generic_array, string, null, typed_array, variant (tag 0x0E legacy extension), map, tuple readers, etc.
**Evidence:**
> include/glaze/beve/read.hpp:3193 `opts{.format = BEVE}` (read_beve)
> include/glaze/beve/read.hpp:3226-3227, 3247-3248 `opt_true<opts{.format = BEVE}, structs_as_arrays_opt_tag{}>` (read_binary_untagged / read_beve_untagged)
> include/glaze/beve/read.hpp:3219 `read<set_beve<Opts>()>(value, buffer, ctx)` (read_file_beve)
> include/glaze/beve/read.hpp:3266 `read_file_beve<opt_true<Opts, structs_as_arrays_opt_tag{}>>(...)` (read_file_beve_untagged)
> include/glaze/beve/read.hpp:3323,3330 `parse<BEVE>::template op<set_beve<Opts>()>(value, ctx, it, end)` (read_beve_delimited, per-element)
> include/glaze/core/opts.hpp:75,79,82 default values of `error_on_unknown_keys`, `error_on_missing_keys`, `partial_read`.
> include/glaze/beve/read.hpp:2928,2942,2976 use of `Opts.error_on_missing_keys`/`Opts.partial_read` gating field-presence tracking in the keyed-object reader.
> include/glaze/beve/read.hpp:1224-1231 `if constexpr (Opts.error_on_unknown_keys) { ctx.error = error_code::unknown_key; ... }` in the variant adjacent-tag reader.

### P2.11 Input Expectations
**Claim:** `read_beve`'s buffer is raw, untrusted, tagged binary (BEVE format): every value begins with a 1-byte type tag (object/array/string/null/typed_array/delimiter/legacy-variant-extension 0x0E, etc., per `glz::tag`), most containers/strings are followed by a "compressed integer" length/count field decoded by `int_from_compressed` (1/2/4/8-byte encodings selected by the low 2 bits of the first byte; the 8-byte form is capped at 2^48 as a safety limit), and object field bodies are additionally length-prefixed per key. The buffer need not be null-terminated for BEVE (that only matters for the JSON/NDJSON text formats); it must not be empty (`no_read_input` otherwise) and must be a byte-sized contiguous range (`sizeof(*buffer.data())==1`). Nesting depth is bounded by `max_recursive_depth_limit` via `depth_guard`, independent of any header field, to prevent stack overflow from a maliciously deep-nested buffer.
**Evidence:**
> include/glaze/beve/header.hpp:232-298 `int_from_compressed`: reads a 1-byte header, dispatches on `config = header & 0b11` to 1/2/4/8-byte compressed integer forms; for the 8-byte form: `static constexpr uint64_t safety_limit = 1ull << 48; if (h > safety_limit) { ctx.error = error_code::unexpected_end; return 0; }`.
> include/glaze/beve/header.hpp:22-27 (file header comment): "Recursion depth: BEVE spends two bytes per nesting level, so input alone can drive the reader arbitrarily deep ... Every reader that consumes an object or generic-array header and then descends into the members takes one level with glz::depth_guard ... bounds the descent at max_recursive_depth_limit and reports exceeded_max_recursive_depth instead of crashing."
> include/glaze/core/read.hpp:140-144: `if constexpr (Opts.format != NDJSON) { if (buffer.empty()) { ctx.error = error_code::no_read_input; ... } }` — empty-buffer rejection applies to BEVE.
> include/glaze/beve/read.hpp:2901-2905 (object/struct reader): `const auto tag = uint8_t(*it); if (tag != header) { ctx.error = error_code::syntax_error; return; }` — tag byte is checked against the expected object header before parsing proceeds.
> include/glaze/beve/read.hpp:2955-2958: `if (uint64_t(end - it) < n || it == end) { ctx.error = error_code::unexpected_end; return; }` — key length is bounds-checked against remaining buffer before being used to index into it.

## Other entries in the Logic Group

### glz::read_beve_untagged (and deprecated alias read_binary_untagged)
**Claim:** Same prerequisites/parameter/return-value contract as `read_beve` (P2.1-P2.8 apply unchanged), but forces `structs_as_arrays=true`, selecting the positional tuple/array struct reader in `from<BEVE,T>` instead of the hashed-key object reader. No init beyond constructing `value`/`buffer`; cleanup is automatic (same stack-based lifecycle). Combines with `read_beve` in one harness as a *second, distinct* decode path over the same (or different) byte buffer — not a flag toggle of the same code, per the Logic Group description.
**Evidence:**
> include/glaze/beve/read.hpp:3245-3249: `return read<opt_true<opts{.format = BEVE}, structs_as_arrays_opt_tag{}>>(std::forward<T>(value), std::forward<Buffer>(buffer));`
> tests/beve_test/beve_test.cpp:2622-2628 ("read_beve_untagged" test): `auto ec = glz::read_beve_untagged(decoded, *encoded);` — same calling convention as read_beve (value, buffer) -> error_ctx-like.
> tests/beve_test/beve_test.cpp:2636-2649 ("read_beve_untagged truncated nested struct"): exercises truncated/malformed input directly against this entry, confirming it is meant to be fuzzed with malformed bytes just like read_beve.

### glz::read_file_beve / glz::read_file_beve_untagged
**Claim:** Requires a readable file path (`sv file_name`) and an output buffer object (e.g. `std::string{}`) that `file_to_buffer` fills; internally builds its own `context`, sets `ctx.current_file`, and on I/O success calls the same `read<set_beve<Opts>()>` core as `read_beve`. Returns `error_ctx`; a file-open/read failure short-circuits with `error_ctx{0, file_error}` before any BEVE bytes are parsed. No separate close/cleanup call — `file_to_buffer`/underlying file stream lifetime is managed internally. In a harness, this entry is only useful if the fuzzer is allowed to control file content (e.g. harness writes fuzz bytes to a temp file first); otherwise prefer `read_beve` directly on an in-memory buffer.
**Evidence:**
> include/glaze/beve/read.hpp:3207-3220: full body quoted above (P2.8).
> include/glaze/file/file_ops.hpp:21,58: `error_code file_to_buffer(T& buffer, auto* file, const std::string_view path)` and `error_code file_to_buffer(T& buffer, const std::string_view file_name)` — the function `read_file_beve` depends on.
> include/glaze/beve/read.hpp:3263-3267 (read_file_beve_untagged): `return read_file_beve<opt_true<Opts, structs_as_arrays_opt_tag{}>>(value, file_name, buffer);` — thin wrapper forcing the untagged (positional) option onto the same file-reading core.
> tests/beve_test/beve_test.cpp:1556-1570 (file_include_test): shows the write_file_beve/read_file_beve round trip pattern and field assertions on the decoded object.

### glz::read_beve_delimited
**Claim:** Operates on a *container* of values (`Container& values`, e.g. `std::vector<T>`) rather than a single `T`; repeatedly invokes `parse<BEVE>::op<set_beve<Opts>()>` per element directly (bypassing the top-level `read<Opts>()` wrapper used by `read_beve`), skipping an optional `tag::delimiter` byte between elements. Requires `Container` to satisfy `readable_array_t<Container> && (emplace_backable<Container> || !resizable<Container>)`. Clears a resizable container first; for non-resizable (fixed-capacity) containers it errors with `exceeded_static_array_size` if more elements are present than capacity allows. Returns `error_ctx` with `count` = bytes consumed up to the failure point (`size_t(it - start)`) on error. Combines with `read_beve` in a harness as a variant/consumer: it reuses the same `from<BEVE,T>`/`parse<BEVE>::op` core per element but adds the delimiter-skipping logic as an extra input dimension to fuzz (malformed/missing/extra delimiters).
**Evidence:**
> include/glaze/beve/read.hpp:3283-3338 (full body quoted above in investigation): note `static_assert(sizeof(decltype(*buffer.data())) == 1);`, `if (buffer.empty()) { ... return {}; }`, `skip_beve_delimiter(it, end)`, `exceeds_capacity(values, index+1, ctx)`, and `parse<BEVE>::template op<set_beve<Opts>()>(value, ctx, it, end)` as the per-element core call.
> include/glaze/beve/read.hpp:3334: `return {size_t(it - start), ctx.error, ctx.custom_error_message};` on error — same error_ctx contract as read_beve.
> tests/beve_test/beve_test.cpp:5391-5405: pairs `glz::write_beve_delimited` with `glz::read_beve_delimited` for round-trip; 5496-5502 tests empty-buffer handling; 5515-5538 test graceful handling of trailing/duplicate delimiters — good fuzz-target evidence for the delimiter dimension.

### glz::write_beve (and write_beve_untagged, write_file_beve)
**Claim:** Inverse operation, not itself part of the attack surface being fuzzed with untrusted bytes, but used in a harness to build valid BEVE corpora for round-trip testing against `read_beve`/`read_beve_untagged`. `write_beve(T&& value, Buffer&& buffer) -> error_ctx` serializes `value` (by const/forwarding ref) into `buffer` (cleared/appended to, depending on overload); `write_beve(T&& value) -> expected<std::string, error_ctx>` returns a freshly-allocated string. No separate destroy step; `buffer`/returned `std::string` lifetime is ordinary RAII.
**Evidence:**
> include/glaze/beve/write.hpp:1836-1840: `template <write_supported<BEVE> T, class Buffer> [[nodiscard]] error_ctx write_beve(T&& value, Buffer&& buffer) { return write<opts{.format = BEVE}>(std::forward<T>(value), std::forward<Buffer>(buffer)); }`
> include/glaze/beve/write.hpp:1842-1846: `template <auto Opts = opts{}, write_supported<BEVE> T> [[nodiscard]] glz::expected<std::string, error_ctx> write_beve(T&& value) { return write<set_beve<Opts>()>(std::forward<T>(value)); }`
> tests/beve_test/beve_test.cpp:97,102 and 1444-1446: `glz::write_beve(src, buffer); ... glz::read_beve(dst, buffer);` and `b = glz::write_beve(v).value_or("error"); auto res = glz::read_beve<my_struct>(b);` — canonical round-trip harness pattern pairing write_beve output directly with read_beve input.

## Build notes
**Claim:** Fuzz harnesses for this project are plain C++ (C++23) translation units compiled directly against the Glaze header-only library (no separate .so/.a to link beyond the fuzzing engine); the target's header (`glaze/beve/read.hpp`, pulled in transitively via `<glaze/glaze.hpp>` and/or `<glaze/beve.hpp>`) is made visible purely through an include-path flag `-Iinclude`, no special defines needed. One translation unit per fuzz target, named after the `.cpp` file stem, each defining `extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t)`, and `main.cpp`/`*exhaustive*` files are explicitly excluded from the per-target build loop.
**Evidence:**
> ossfuzz_view/glaze-c1/build.sh:18: `fuzzing/ossfuzz.sh` — delegates straight to the repo's own fuzzing build script.
> glaze/fuzzing/ossfuzz.sh:11-15:
> ```
> for SRCFILE in $(ls fuzzing/*.cpp |grep -v -E '(exhaustive|main\.cpp)'); do
>     NAME=$(basename $SRCFILE .cpp)
>     $CXX $CXXFLAGS -std=c++23 -g -Iinclude \
>          $SRCFILE -o $OUT/$NAME \
>          $LIB_FUZZING_ENGINE
> done
> ```
> ossfuzz_view/glaze-c1/Dockerfile:19-21: `RUN git clone --depth 1 https://github.com/stephenberry/glaze` / `WORKDIR glaze` / `COPY build.sh run_tests.sh $SRC/` — confirms header-only checkout, no separate build/install step for a library artifact; `-Iinclude` in ossfuzz.sh resolves directly against the cloned repo's `include/` tree where `glaze/beve/read.hpp` lives.
> glaze/fuzzing/msgpack_reflection.cpp:1-6 (sibling example, same pattern expected for a BEVE fuzzer): `#include <glaze/glaze.hpp>` plus a format-specific header (e.g. `<glaze/msgpack.hpp>`); for BEVE the analogous include is `<glaze/beve.hpp>` (confirmed present via `include/glaze/beve/read.hpp`'s own include graph — `glaze/beve/read.hpp` is the file the target is declared in, reachable once `<glaze/beve.hpp>` or `<glaze/glaze.hpp>` is included).
> glaze/fuzzing/jsonb_reflection.cpp:16-58: shows the existing harness convention of using `std::vector<char>` (not `std::string`) to avoid small-string-optimization masking out-of-bounds bugs, and of calling the typed reader, a converter, and a schemaless/generic reader as three distinct checks in one `LLVMFuzzerTestOneInput` — the same three-path pattern (typed read_beve, read_beve_untagged, read_beve_delimited) is the template the downstream harness for BEVE should follow.
> Note: there is currently **no existing `fuzzing/beve_*.cpp` harness** in the repository's `fuzzing/` directory (only `binary_reflection.cpp`, which in this checkout is a stub/placeholder: `/* Gold harness replaced by an empty stub for baseline generation. */`), so the downstream harness for `read_beve` will be a new file following the `ossfuzz.sh` loop convention (any `fuzzing/*.cpp` not matching `exhaustive`/`main.cpp` is auto-picked-up and compiled as its own fuzz target).
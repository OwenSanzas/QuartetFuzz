## target: glz::read_cbor

### P2.0 Main path to keep (every input) — read first
**Claim:** Per input, with the full input as the CBOR buffer: (1) `glz::read_cbor(value, buffer)` into one reflected struct whose members span scalars, string, fixed array, optional, map, vector of nested struct, variant and chrono time_point/epoch_seconds; (2) on success `glz::write_cbor(value, out)` and `glz::write_json(value, json)`, on failure `glz::format_error(ec)` / `glz::format_error(ec, buffer)`; (3) `glz::read_cbor<T>(buffer)`; (4) `glz::read_cbor<glz::generic>(buffer)` followed by `glz::write_json(generic)` and `glz::is_array`/`glz::is_object`; (5) the same struct read again from a mutable, '\0'-terminated copy of the buffer.
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> include/glaze/json/generic_fwd.hpp:110-117: `template <num_mode Mode = num_mode::f64, template <class> class MapType = ordered_small_map> struct generic_json { ... using object_t = MapType<generic_json<Mode, MapType>>;` and :551 `using generic = generic_json<num_mode::f64>;` — a variant-backed value type any glaze reader (incl. CBOR's variant/map/array readers) can fill.
> include/glaze/core/reflect.hpp:3333-3359: `format_error(const error_code&)`, `format_error(const error_ctx&)`, `format_error(const error_ctx&, const auto& buffer)`.
> docs/cbor.md:24 `auto ec = glz::read_cbor(s, buffer);`; docs/chrono.md:405 CBOR datetime tags for chrono types (tag 0 string / tag 1 epoch).
> fuzzing/cbor_roundtrip_int.cpp:34-35 / cbor_roundtrip_string.cpp:25-26: `read_cbor<T>` expected overload, check then `.value()`.
> include/glaze/cbor/read.hpp:2677: `read_cbor` = `read<opts{.format = CBOR}>` — destination type T alone selects which from<CBOR,T> specializations run, so a struct with many member kinds reaches many of them in one call.

### P2.1 Init Sequence
**Claim:** No library-level global init call is required before `read_cbor`. The only "init" is constructing/obtaining the destination value `T` (default-constructed by the harness, or default-constructed internally by the Buffer-only overload) and having a populated byte buffer to decode. No handle/context object needs to be created by the caller; `glz::read<Opts>` builds its own `context ctx{}` internally when the caller does not supply one.
**Evidence:**
> include/glaze/cbor/read.hpp:2674-2678: `read_cbor(T&& value, Buffer&& buffer) { return read<opts{.format = CBOR}>(value, std::forward<Buffer>(buffer)); }` — no other setup call.
> include/glaze/core/read.hpp:216-222: `read<Opts>(T& value, Buf&& buffer)` overload (the one `read_cbor` reaches, since it passes no ctx) constructs `format_context_t<Opts.format> ctx{};` itself and forwards to the ctx-taking overload.
> include/glaze/cbor/read.hpp:2680-2689: the Buffer-only `read_cbor` overload does `T value{};` then calls the same `read<...>` path — confirms default construction is sufficient, no factory function needed.

### P2.2 Parameter Construction
**Claim:** Parameter 1 (`value`) is any type satisfying `read_supported<T, CBOR>` i.e. `from<CBOR, std::remove_cvref_t<T>>` must exist (struct/reflectable, container, map, variant, numeric, string, chrono, etc.); passed by forwarding reference, normally a stack-local default-constructed object or reference to an existing one — no heap allocation required by the caller. Parameter 2 (`buffer`) must be a `contiguous` byte range (checked via `static_assert(sizeof(decltype(*buffer.data())) == 1)` in `glz::read`), e.g. `std::string`, `std::vector<char/uint8_t>`, `std::string_view`, or a `std::span`; it must not be empty (an empty buffer immediately errors with `no_read_input`).
**Evidence:**
> include/glaze/cbor/read.hpp:2674: `template <read_supported<CBOR> T, class Buffer> ... read_cbor(T&& value, Buffer&& buffer)`
> include/glaze/core/opts.hpp:1580: `concept read_supported = requires { from<Format, std::remove_cvref_t<T>>{}; };`
> include/glaze/core/read.hpp:137: `static_assert(sizeof(decltype(*buffer.data())) == 1);` inside the underlying `read<Opts>` that validates the buffer is byte-addressable/contiguous.
> include/glaze/core/read.hpp:140-144: `if (buffer.empty()) { ctx.error = error_code::no_read_input; return {0, ctx.error, ...}; }`

### P2.3 Object Lifecycle
**Claim:** There is no persistent "object" created by `read_cbor` with its own create/destroy API; the lifecycle is: construct/obtain destination value `T` → construct/obtain CBOR byte buffer (typically via `glz::write_cbor` producing it, or raw fuzz bytes) → call `read_cbor(value, buffer)` (or the Buffer-only overload) → inspect the returned `error_ctx`/`expected<T,error_ctx>` → use `value`/`.value()` → ordinary scope-exit destructor cleanup of `value` and `buffer` (RAII, no explicit "close"/"destroy" call exists).
**Evidence:**
> tests/cbor_test/cbor_test.cpp:326-333: `bool v = true; std::string buffer; expect(not glz::write_cbor(v, buffer)); bool result{}; expect(not glz::read_cbor(result, buffer)); expect(result == v);` — full construct→write→read→use→(implicit destroy) cycle.
> include/glaze/cbor/read.hpp:2681-2689: `expected<T,error_ctx> read_cbor(Buffer&& buffer) { T value{}; ... return value; }` — value construction and the read are fused; caller only manages the `expected` result afterward.

### P2.4 Return Value Handling
**Claim:** `read_cbor(T&, Buffer&&)` returns `error_ctx`, which is truthy on error (`operator bool() const { return ec != error_code::none; }`); callers must check it before trusting `value`. The Buffer-only overload returns `glz::expected<T, error_ctx>`; callers must check `if (pe)`/`restored` (bool-convertible, false means error) before calling `.value()`. On error, `value`/`T` may be partially modified (parsing can error mid-structure) and must not be treated as fully valid.
**Evidence:**
> include/glaze/core/context.hpp:126-136: `struct error_ctx final { size_t count{}; error_code ec{}; std::string_view custom_error_message{}; operator bool() const noexcept { return ec != error_code::none; } ... };`
> include/glaze/cbor/read.hpp:2684-2688: `const auto pe = read<...>(value, ...); if (pe) { return unexpected(pe); } return value;`
> tests/cbor_test/cbor_test.cpp:331-332: `expect(not glz::read_cbor(result, buffer)); expect(result == v);` — error_ctx checked with `not` before using `result`.
> fuzzing/cbor_roundtrip_int.cpp:33-35: `auto restored = glz::read_cbor<S>(buffer); assert(restored); assert(restored.value().value == s.value);` — expected-style overload checked truthy before `.value()`.

### P2.5 Cleanup Sequence
**Claim:** No explicit free/close/destroy function exists for CBOR read results; all resources (`T value`, `std::string`/`std::vector` buffer, internal `context`) are stack-allocated / RAII-managed and destroyed automatically at scope exit, on both success and error paths. `read_file_cbor` additionally only touches a caller-provided buffer (no file handle leaks across the call since `file_to_buffer` opens/closes the file internally).
**Evidence:**
> include/glaze/cbor/read.hpp:2692-2704: `read_file_cbor` calls `file_to_buffer(buffer, ctx.current_file)` (opens+reads+closes internally) then `read<set_cbor<Opts>()>(value, buffer, ctx)`; no handle returned to caller to close.
> include/glaze/core/read.hpp:205-213: `finish:` label performs `finalize_top_level_read` and (if padding was applied) `buffer.resize(original_size)` to restore buffer state on every path (success or error) before returning — this is the only "cleanup" step, and it is internal, not caller-visible.

### P2.6 API Existence
**Claim:** All functions discussed are present in the current codebase (verified by direct reads below), none are fabricated.
**Evidence:**
> include/glaze/cbor/read.hpp:2675: `read_cbor(T&& value, Buffer&& buffer)` exists.
> include/glaze/cbor/read.hpp:2681: `read_cbor(Buffer&& buffer)` (expected-returning overload) exists.
> include/glaze/cbor/read.hpp:2692: `read_file_cbor(T& value, const sv file_name, auto&& buffer)` exists.
> include/glaze/cbor/write.hpp:1227: `write_cbor(T&& value, Buffer&& buffer)` exists.
> include/glaze/cbor/write.hpp:1233: `write_cbor(T&& value)` (expected-returning overload) exists.
> include/glaze/cbor/write.hpp:1239: `write_file_cbor(T&& value, const sv file_name, auto&& buffer)` exists.
> include/glaze/core/read.hpp:135,218: underlying `read<Opts>` overloads exist.
> include/glaze/file/file_ops.hpp:58: `file_to_buffer(T& buffer, const std::string_view file_name)` exists.
> include/glaze/core/context.hpp:126,141: `error_ctx`, `context` exist.

### P2.7 Co-call Constraints
**Claim:** `write_cbor` / `read_cbor` are the natural encode/decode pair used throughout the test and fuzz suites (not a strict lock/unlock, but every roundtrip test pairs them 1:1: write first into a fresh buffer, then read from that same buffer). `read_file_cbor` pairs with `write_file_cbor` the same way but through a named file. There is no begin/commit style pairing — each call is a complete, self-contained operation.
**Evidence:**
> tests/cbor_test/cbor_test.cpp:329-331: `glz::write_cbor(v, buffer)` immediately followed by `glz::read_cbor(result, buffer)` in the same test lambda — repeated ~90+ times across the file (lines 329-4600+).
> fuzzing/cbor_roundtrip_int.cpp:29-33, cbor_roundtrip_string.cpp:20-24, cbor_roundtrip_floating.cpp:32-36: identical write-then-read pairing pattern in all three roundtrip fuzzers.
> include/glaze/exceptions/cbor_exceptions.hpp:33-41,69-77: `read_file_cbor`/`write_file_cbor` exception-throwing wrappers exist as a pair, confirming the file-based pairing is a recognized idiom.

### P2.8 Prerequisite State
**Claim:** No external connections/global state are required. The only prerequisite is that `T` have a `glz::from<CBOR, T>` specialization reachable through reflection (`glz::meta<T>`, a `glaze` member struct, or an auto-reflectable aggregate), which is a compile-time constraint enforced by the `read_supported<CBOR>` concept, not a runtime setup step.
**Evidence:**
> include/glaze/core/opts.hpp:1580: `concept read_supported = requires { from<Format, std::remove_cvref_t<T>>{}; };`
> tests/cbor_test/cbor_test.cpp:51-59: example `glz::meta<my_struct>` specialization providing the reflection info consumed by `from<CBOR, my_struct>`.
> fuzzing/cbor_roundtrip_int.cpp:10-14: `template <typename T> struct Value { T value{}; };` — an aggregate used directly without an explicit `glz::meta` (auto-reflected), confirming structs need no extra runtime registration, only to be default-constructible/reflectable.

### P2.9 Result Usage
**Claim:** After `read_cbor` succeeds, callers read fields/elements directly off the filled `value` (equality checks, further processing) with no extra "finalize" API; for the `expected<T,error_ctx>` overload, callers call `.value()` to obtain `T` (or `*restored`), after checking truthiness. No other glaze API is required to consume the result — it's a plain C++ object from here on. `read_file_cbor`'s result is used the same way as `read_cbor`'s.
**Evidence:**
> tests/cbor_test/cbor_test.cpp:332: `expect(result == v);` — direct use of decoded value.
> fuzzing/cbor_roundtrip_int.cpp:34-35: `assert(restored); assert(restored.value().value == s.value);` — `.value()` used to extract `T` then its member accessed directly.
> fuzzing/cbor_roundtrip_string.cpp:25-26: `assert(restored); assert(restored.value().value == s.value);` — same pattern for `std::string` member.
> include/glaze/cbor/read.hpp:2681-2689 defines the `expected<T,error_ctx>` contract that callers rely on (`.value()` member from `glz::expected`, modeled on `std::expected`).

### P2.10 Behaviour Controls
**Claim:** Behaviour is controlled by (a) compile-time `Opts` template parameter (`opts{.format = CBOR}` is fixed by `read_cbor` itself, but `read_file_cbor<Opts>` lets a caller pass additional flags merged via `set_cbor<Opts>()`, e.g. `error_on_unknown_keys`, padding-disable, partial-read flags defined in `glz::opts`), and (b) the chosen `T` (struct/array/map/variant/number/string/chrono wrapper), which selects which `from<CBOR,T>` / `decode_hash_with_size` / `skip_value<CBOR>` / `emplace_runtime_variant` code path runs. At the byte level, the CBOR major-type + additional-info of the input buffer is what steers `cbor_detail::decode_arg` and the per-type `from<CBOR,T>::op` implementations (fixed vs. indefinite-length containers, tag-based typed arrays/datetime/decimal, 2-element `[index,value]` variant arrays).
**Evidence:**
> include/glaze/cbor/read.hpp:2677: `read<opts{.format = CBOR}>(...)` — format fixed to CBOR for `read_cbor`.
> include/glaze/cbor/read.hpp:2691,2703: `read_file_cbor<Opts = opts{}>` forwards `set_cbor<Opts>()` giving callers a way to merge other `opts` flags (e.g. error handling) while targeting CBOR.
> include/glaze/cbor/read.hpp:27-90 (`cbor_detail::decode_arg`): additional_info 0-23 is a direct value; 24/25/26/27 read uint8/16/32/64 big-endian-converted length/argument; else `syntax_error`.
> include/glaze/cbor/read.hpp:2242-2297 (variant decoder): major type must be `array`, `count` must be exactly 2 `[index, value]`, `type_index >= std::variant_size_v<T>` -> `no_matching_variant_type`, else `emplace_runtime_variant` then recursive `parse<CBOR>::op`.
> include/glaze/cbor/read.hpp:123-143 (`parse<CBOR>::op`): const values route to `skip_value<CBOR>::op` instead of `from<CBOR,V>::op`, controlled by `check_error_on_const_read(Opts)`.

**Additional behaviour controls (previously missing):** the destination `glz::generic` (schemaless) is a separate path from any typed T: it accepts every CBOR major type and builds ordered_small_map objects; result usage `glz::write_json` on typed and generic results runs the JSON writer on input-derived data. Varying T should happen by adding members to one struct (all decoded per call), not by a per-input type selector that removes inputs from the main struct.

### P2.11 Input Expectations
**Claim:** The input buffer is a raw CBOR-encoded byte sequence (RFC 7049/8949 style): it is not required to be null-terminated (CBOR reading does not rely on a sentinel, unlike some JSON paths) but IS required to be non-empty, and its length must be sufficient for every length-prefixed field it claims to hold — each decode step checks `it >= end` / `(it + n) > end` before reading multi-byte arguments or payloads and sets `unexpected_end` otherwise; malformed major/additional-info combinations yield `syntax_error`; nesting depth (maps/arrays, including variant's own array framing) is bounded by `max_recursive_depth_limit` (256) via `glz::depth_guard`, exceeding it yields `exceeded_max_recursive_depth`.
**Evidence:**
> include/glaze/core/read.hpp:140-144: `if (buffer.empty()) { ctx.error = error_code::no_read_input; ... }` — non-empty buffer required.
> include/glaze/cbor/read.hpp:38,48,61,74 (`decode_arg`): `if (it >= end) ... unexpected_end;` / `if ((it+2) > end) ...` etc. before every multi-byte length read.
> include/glaze/cbor/read.hpp:86-89: `default: ctx.error = error_code::syntax_error;` for an invalid additional_info value in `decode_arg`.
> include/glaze/core/context.hpp:15: `inline constexpr size_t max_recursive_depth_limit = 256;`
> include/glaze/core/context.hpp:172-192 (`depth_guard`): increments `ctx.depth` on entry, sets `exceeded_max_recursive_depth` and refuses to "enter" once `ctx.depth >= max_recursive_depth_limit`.
> include/glaze/cbor/read.hpp:16-20 (file-top comment): "a CBOR nesting level costs a single byte, so input alone can drive the reader arbitrarily deep... glz::depth_guard... bounds the descent at max_recursive_depth_limit."

## Other entries of the Logic Group

### glz::read_file_cbor (source/variant entry)
**Claim/Protocol:** Signature: `template <auto Opts = opts{}, read_supported<CBOR> T> error_ctx read_file_cbor(T& value, const sv file_name, auto&& buffer)`. Prerequisite: a readable file at `file_name`; `buffer` is caller-provided storage (reused across calls) that gets overwritten by the file's bytes. Internally builds its own `context ctx{}`, sets `ctx.current_file = file_name` (so error paths report the file name), calls `file_to_buffer(buffer, ctx.current_file)` — on failure returns `error_ctx{file_error}` immediately without touching `value`. On success it calls the SAME core path as `read_cbor`: `read<set_cbor<Opts>()>(value, buffer, ctx)`. Return: `error_ctx`, same semantics as the target. Cleanup: none beyond normal destructors; the file is opened/closed inside `file_to_buffer`. In a harness: cannot be driven purely from an in-memory fuzz buffer unless the harness first writes the fuzz bytes to a temp file and passes its path — this is the one entry that needs an actual filesystem artifact.
**Evidence:**
> include/glaze/cbor/read.hpp:2691-2704.
> include/glaze/file/file_ops.hpp:58-61 (`file_to_buffer` signature read from path).
> include/glaze/core/opts.hpp:1513-1519 (`set_cbor<Opts>()` merges CBOR format into caller-chosen `Opts`).

### glz::write_cbor (inverse/source entry, needed to build decode input / round-trip)
**Claim/Protocol:** Two overloads: `error_ctx write_cbor(T&& value, Buffer&& buffer)` (writes into caller buffer, returns `error_ctx`) and `glz::expected<std::string, error_ctx> write_cbor(T&& value)` (returns an owned `std::string` or error). Prerequisite: `T` must satisfy `write_supported<CBOR>` (analogous `to<CBOR,T>` specialization exists — not independently verified here but symmetric to `read_supported`). No init needed beyond constructing `value`. Return value must be checked (`error_ctx` truthy = error) before trusting `buffer`'s contents. In the project's own fuzzers/tests this is always called immediately before `read_cbor` on the same buffer, to produce a syntactically-valid CBOR payload that is then decoded (round-trip) or, for a pure-decode harness, to serve as a structurally-correct seed that the harness mutates before feeding to `read_cbor`.
**Evidence:**
> include/glaze/cbor/write.hpp:1226-1236.
> tests/cbor_test/cbor_test.cpp:329 (`expect(not glz::write_cbor(v, buffer));`) consistently precedes `read_cbor` in every test block (lines 329-4600+).
> fuzzing/cbor_roundtrip_int.cpp:29-33, cbor_roundtrip_string.cpp:20-24, cbor_roundtrip_floating.cpp:32-43 — `write_cbor` always called first, its `error_ctx` checked (`if (write_ec) return;`) before proceeding to `read_cbor`.

### glz::write_file_cbor (inverse/source entry, file variant)
**Claim/Protocol:** `template <auto Opts = opts{}, write_supported<CBOR> T> error_code write_file_cbor(T&& value, const sv file_name, auto&& buffer)`. Calls `write<set_cbor<Opts>()>(value, buffer)` into the caller buffer; on success calls `buffer_to_file(buffer, file_name)` to persist it. Returns plain `error_code` (not `error_ctx`) — different return type than the other write/read entries, so a harness must check it with the enum directly / `bool(ec)` semantics of `error_code`'s own operator, not `error_ctx`'s. Pairs with `read_file_cbor` for a full file round trip; not required if the harness drives `read_cbor`/`read_file_cbor` directly from fuzz-provided bytes.
**Evidence:**
> include/glaze/cbor/write.hpp:1238-1246: `error_code write_file_cbor(...) { const auto ec = write<set_cbor<Opts>()>(...); if (bool(ec)) return ec.ec; return buffer_to_file(buffer, file_name); }`
> include/glaze/exceptions/cbor_exceptions.hpp:69-77: exception-throwing wrapper confirms `write_file_cbor` is treated as paired with `read_file_cbor` (`"write_file_cbor error for: " + file_name` style messages).

## Build notes
**Claim:** Fuzzers are plain C++23 translation units, compiled/linked directly by `$CXX` with `-Iinclude` (header-only library, no separate lib to link beyond the fuzzing engine), one binary per `fuzzing/*.cpp` file (excluding files matching `exhaustive` or `main.cpp`). `glz::read_cbor`/`write_cbor` are brought in via `#include <glaze/cbor.hpp>` (which pulls in `cbor/read.hpp`, `cbor/write.hpp`, etc.) or the umbrella `#include <glaze/glaze.hpp>`.
**Evidence:**
> fuzzing/ossfuzz.sh:11-16: ```for SRCFILE in $(ls fuzzing/*.cpp |grep -v -E '(exhaustive|main\.cpp)'); do NAME=$(basename $SRCFILE .cpp); $CXX $CXXFLAGS -std=c++23 -g -Iinclude $SRCFILE -o $OUT/$NAME $LIB_FUZZING_ENGINE; done```
> build.sh:18 (project root): `fuzzing/ossfuzz.sh` — build.sh simply delegates to this script.
> ossfuzz_view/glaze-c2/Dockerfile:17-21: `FROM gcr.io/oss-fuzz-base/base-builder` then `git clone glaze`, `COPY build.sh run_tests.sh $SRC/` — standard OSS-Fuzz base builder image providing `$CXX`, `$CXXFLAGS`, `$LIB_FUZZING_ENGINE`.
> fuzzing/cbor_roundtrip_int.cpp:5-6: `#include <glaze/cbor.hpp>` / `#include <glaze/glaze.hpp>` — the include pattern real fuzzers use (resolved via the `-Iinclude` path above, since the header lives at `include/glaze/cbor.hpp`).
> include/glaze/cbor.hpp:6-10: pulls in `cbor/cbor_to_json.hpp`, `cbor/header.hpp`, `cbor/read.hpp`, `cbor/wrappers.hpp`, `cbor/write.hpp` — header-only, no separate .a/.so to link.
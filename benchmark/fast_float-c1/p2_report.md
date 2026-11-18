## target: fast_float::from_chars (template, float overload)

### P2.1 Init Sequence
**Claim:** No library-level initialization object/context is required. `fast_float::from_chars` is a free function template; the only "setup" is preparing the input buffer `[first,last)` and a `T value` to receive the result. No prior call to any `fast_float::*_init`/constructor API is needed; the library is header-only and stateless across calls.
**Evidence:**
> include/fast_float/fast_float.h:33-37: `template <typename T, typename UC=char, ...> from_chars_result_t<UC> from_chars(UC const *first, UC const *last, T &value, chars_format fmt = chars_format::general) noexcept;` — a plain function, no handle/context parameter.
> include/fast_float/parse_number.h:183-189: definition simply forwards to `from_chars_caller<T>::call(first,last,value, parse_options_t<UC>(fmt))` — no global state touched.
> tests/example_test.cpp:7-11 and tests/json_fmt.cpp:7-14: every call site just declares local variables (`std::string input; double result;`) and calls `from_chars` directly, no prior init call.

### P2.2 Parameter Construction
**Claim:** `first`/`last` must be a valid, non-null (unless first==last), readable `UC` buffer with `first <= last`; `value` is an output reference of a supported floating type; `fmt` is a `chars_format` bit combination (default `general`).
**Evidence:**
> include/fast_float/fast_float.h:33-34: `typename = FASTFLOAT_ENABLE_IF(is_supported_float_type<T>::value)` — T restricted at compile time to `float`, `double` (and optionally `std::float32_t`/`float64_t` when available).
> include/fast_float/float_common.h:260-277: `is_supported_float_type` enumerates the allowed T.
> include/fast_float/float_common.h:289-294: `is_supported_char_type` restricts UC to `char, wchar_t, char16_t, char32_t` (enforced via `static_assert` inside `from_chars_float_advanced`, parse_number.h:319-322).
> include/fast_float/parse_number.h:332-336: `if (first == last) { answer.ec = std::errc::invalid_argument; answer.ptr = first; return answer; }` — empty range is handled, not UB, but `first > last` is never checked (caller contract: `first <= last`).
> include/fast_float/float_common.h:71-82: `parse_options_t<UC>` has `format`, `decimal_point` (defaults to `UC('.')`), `base` (defaults 10, unused by the float path) — constructed implicitly by `from_chars` from `fmt` via `parse_options_t<UC>(fmt)` (parse_number.h:188).

### P2.3 Object Lifecycle
**Claim:** There is no heap-allocated object/handle lifecycle: `from_chars` takes raw pointers to caller-owned memory and an output reference; it allocates nothing (`noexcept`, no `new`/`malloc`) and there is nothing to destroy afterward.
**Evidence:**
> fast_float.h:22-23 (doc comment above from_chars): "The implementation does not throw and does not allocate memory (e.g., with `new` or `malloc`)."
> include/fast_float/fast_float.h:35-37: return type `from_chars_result_t<UC>` is returned by value (simple struct of `ptr`+`ec`), no owning resource.
> include/fast_float/float_common.h:59-67: `from_chars_result_t<UC>` is a POD struct `{UC const *ptr; std::errc ec;}` with an `operator bool()`; nothing to free.

### P2.4 Return Value Handling
**Claim:** Caller must check `answer.ec` (an `std::errc`) or use `operator bool()`; `answer.ptr` indicates how far parsing advanced (valid even on partial success/failure) and should be used to resume/validate input consumption.
**Evidence:**
> include/fast_float/float_common.h:59-67: `operator bool() const noexcept { return ec == std::errc(); }`.
> tests/example_test.cpp:12-14 / 84-91 / 100-107: checks `answer.ec != std::errc()`, compares `answer.ec` to `std::errc::result_out_of_range`, and checks `r.ptr` value to confirm how much was consumed on overflow/underflow.
> include/fast_float/parse_number.h:393-397: `if ((pns.mantissa != 0 && am.mantissa == 0 && am.power2 == 0) || am.power2 == binary_format<T>::infinite_power()) { answer.ec = std::errc::result_out_of_range; }` — overflow/underflow sets this errc while still writing a value (0 or inf) into `value`.
> parse_number.h:346-354: when the number is not a valid numeral, falls back to `detail::parse_infnan`, which itself can set `answer.ec = std::errc::invalid_argument` (parse_number.h:69) — `value` may be left unmodified or set to NaN/Inf depending on branch, so callers must gate use of `value` on `ec`.
> tests/example_test.cpp:50-56 (many_loop) shows advancing a cursor with `pointer = answer.ptr;` even across multiple consecutive from_chars calls on the same buffer (comma separated input), confirming `answer.ptr` is meant to be reused as the next `first`.

### P2.5 Cleanup Sequence
**Claim:** No cleanup needed — all parameters/results are value/pointer types owned by the caller; `from_chars` never takes ownership.
**Evidence:**
> fast_float.h:22-23: no allocation claim (quoted above).
> float_common.h:59-67: result struct contains only a non-owning pointer (`ptr`, into the caller's buffer) and an errc value.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the given paths.
**Evidence:**
> include/fast_float/fast_float.h:33-37 `from_chars` (float overload, declaration).
> include/fast_float/fast_float.h:44-46 `from_chars_advanced` (options overload, declaration).
> include/fast_float/fast_float.h:85-86 `from_chars` (integer, base overload, declaration).
> include/fast_float/parse_number.h:183-189 `from_chars` (float) definition.
> include/fast_float/parse_number.h:401-413 `from_chars` (integer) definition.
> include/fast_float/parse_number.h:314-317 `from_chars_float_advanced` definition.
> include/fast_float/parse_number.h:493-519 `from_chars_int_advanced` definition.
> include/fast_float/parse_number.h:543-551 `from_chars_advanced` (dispatcher on type) definition.
> include/fast_float/parse_number.h:304-312 `parse_number_slow_path` definition.
> include/fast_float/ascii_number.h:562-564 `parse_int_string` definition (signature continues below, body through ~line 840).
> include/fast_float/parse_number.h:22-71 `detail::parse_infnan` definition.
> include/fast_float/ascii_number.h (detail::parse_number_string) — found via grep at decl near line 319+ (`template <typename UC>` just after parsed_number_string_t, ascii_number.h:319).
> compute_float / compute_error referenced in decimal_to_binary.h (used at parse_number.h:380-381, 386-387 as `compute_float<binary_format<T>>`).
> digit_comp family referenced in digit_comparison.h, included by parse_number.h:6.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for `from_chars`; the only "pairing" pattern is iterative re-invocation on the *same* buffer, feeding the previous call's `answer.ptr` as the next call's `first`, to parse multiple numbers from one text blob (e.g. comma/whitespace separated streams). There is no explicit open/close.
**Evidence:**
> tests/example_test.cpp:7-41 (`many()`), 43-61 (`many_loop()`): repeatedly calls `fast_float::from_chars(pointer, end_pointer, result)` reusing `answer.ptr` as next `pointer`.
> tests/example_comma_test.cpp:10-12: single call with custom `decimal_point` via `parse_options`, no pairing needed.

### P2.8 Prerequisite State
**Claim:** The only prerequisite state is: a valid memory range `[first,last)` of `UC` the caller owns and guarantees stays alive and unmodified during the call, and a default-constructible/assignable `T value` lvalue. No global/thread state, no file/network/connection objects.
**Evidence:**
> include/fast_float/fast_float.h:33-37 signature takes raw pointers only.
> include/fast_float/parse_number.h:327-336: function directly dereferences/scans `[first,last)` (`fast_float::is_space(*first)` etc.) assuming it is valid readable memory — no other prerequisite object.
> Caveat found in code: `detail::parse_infnan` (parse_number.h:29-30) comment "assume first < last, so dereference without checks" — i.e., `from_chars_float_advanced` already checked `first != last` (parse_number.h:332-336) before calling into any code path that dereferences `*first`, so the prerequisite "first < last" is enforced internally before any dereference; a harness need not pre-check this itself, but `first <= last` (no overlap/wrap) must hold as a caller invariant.

### P2.9 Result Usage
**Claim:** Downstream, `answer.ec`/`operator bool()` is the gate for trusting `value`; `answer.ptr` is used either to detect trailing/junk characters, to resume parsing more tokens from the same buffer, or (in the bare bool usage) simply discarded. No fast_float API "consumes" `value` further — `value` is a plain `T` used directly by the caller's own code (printed, compared, stored) once accepted.
**Evidence:**
> tests/json_fmt.cpp:96-126: loops over expected (value, trailing junk) pairs; after `from_chars`, reconstructs `s.substr(answer.ptr - s.data())` from `answer.ptr` to double check "junk_chars" match, and compares `result` to `expected.value` (project-level logic, not another fast_float API).
> tests/example_test.cpp:17-20: checks `answer.ptr[0] == ','` to detect the separator after a parsed number, then feeds `answer.ptr + 1` back into `from_chars` as new `first` — i.e. the *only* fast_float function that consumes `answer.ptr` is `from_chars` itself (chained calls).
> tests/ipv4_test.cpp:47 (`auto r = fast_float::from_chars(current, end, value);`) — `value`/`r.ptr` then used by the caller's own IPv4-construction logic (not shown as a fast_float call), consistent with "value escapes the library".

### P2.10 Behaviour Controls
**Claim:** `fmt` (via `parse_options_t.format`) selects which number grammars are accepted (fixed/scientific/general/json/json_or_infnan/fortran/no_infnan/allow_leading_plus/skip_white_space); `parse_options_t.decimal_point` changes the character used as decimal separator; for the integer overloads, `base` (2..36) and the signedness of `T` change digit interpretation; `UC` selects the character width.
**Evidence:**
> include/fast_float/float_common.h:44-57: `chars_format` enum bit values and composite flags (`json`, `json_or_infnan`, `fortran`, `general`).
> include/fast_float/parse_number.h:324, 337, 347: `fmt & basic_json_fmt` chooses the `parse_number_string<true,...>` template instantiation (stricter JSON grammar) vs `<false,...>`; `fmt & chars_format::no_infnan` disables the inf/nan fallback path (returns `invalid_argument` instead of calling `detail::parse_infnan`).
> include/fast_float/parse_number.h:327-331 and 507-511: `fmt & chars_format::skip_white_space` toggles a leading-whitespace skip loop (both in `from_chars_float_advanced` and `from_chars_int_advanced`).
> include/fast_float/parse_number.h:30-34 (`detail::parse_infnan`): `fmt & chars_format::allow_leading_plus` allows a leading `'+'` before `nan`/`inf`.
> tests/example_comma_test.cpp:10: `fast_float::parse_options options{fast_float::chars_format::general, ','};` — custom `decimal_point` changes the digit/fraction separator the scanner looks for.
> tests/fortran.cpp:12-16: `chars_format::fortran` activates recognizing `d`/`D` exponent markers (basic_fortran_fmt bit), verified by inputs like `"1d+4"`.
> include/fast_float/parse_number.h:410-412 (integer `from_chars`): `options.base = base;` is set directly from the caller's `base` argument, then passed to `from_chars_advanced` → `from_chars_int_advanced` → `parse_int_string`, where base in [2,36] (checked parse_number.h:512) selects digit-value decoding; `std::is_signed<T>::value` (ascii_number.h:578) gates whether a leading `-` is permitted.

### P2.11 Input Expectations
**Claim:** Input is a half-open byte/char range `[first,last)`, no NUL-termination required (length given explicitly by `last-first`), optionally preceded by whitespace (only if `skip_white_space` set) and sign, followed by digits/decimal point/exponent or one of the special tokens `inf`/`infinity`/`nan`/`nan(...)` (case-insensitive) unless `no_infnan` is set; trailing characters after the recognized number are not consumed and are reported via `answer.ptr` (no requirement that the whole range be consumed). Internally the algorithm distinguishes short (<=19 significant digit) fast paths from arbitrary-length (bignum) slow paths for `too_many_digits`.
**Evidence:**
> fast_float.h:9-11 (doc comment): "parses the character sequence [first,last) for a number... locale-independent format equivalent to ... std::strtod in the default (\"C\") locale."
> include/fast_float/parse_number.h:332-336: empty range → `invalid_argument`, not a crash — confirms `first==last` is a valid (though erroneous) call, not UB, while `first>last` or non-contiguous invalid memory is undefined (caller responsibility, never checked).
> include/fast_float/parse_number.h:29 / 37 (`detail::parse_infnan`): uses `last - first >= 3` and `>= 8` length checks before scanning `nan`/`infinity` tokens — no NUL-termination assumed, relies purely on `last`.
> include/fast_float/parse_number.h:356-371 and ascii_number.h:525-555 (`too_many_digits`/`store_spans`): indicates the >19-significant-digit path that forces `parse_number_slow_path` (bignum fallback) rather than Clinger/Eisel-Lemire fast paths.
> include/fast_float/parse_number.h:44-53 (`detail::parse_infnan` nan payload loop): `nan(` followed by `[A-Za-z0-9_]*` then `)`, otherwise treated as a plain `nan` with junk after it (`ptr` not advanced past the malformed parenthetical) — bounded strictly by `last`.

## Other Logic-Group Entries

### Entry: fast_float::from_chars_advanced (2-arg options overload used as a direct variant of the target)
**Claim:** Signature `from_chars_advanced(UC const *first, UC const *last, T &value, parse_options_t<UC> options) noexcept` (fast_float.h:44-46, defined parse_number.h:543-551). Requires no init beyond constructing a `parse_options_t<UC>` (via its constructor `parse_options_t(chars_format fmt=general, UC dot='.', int b=10)`, float_common.h:72-74) or via aggregate init as in tests (`fast_float::parse_options options{chars_format::json, ','}`, tests/json_fmt.cpp:10-12 and tests/example_comma_test.cpp:10). It dispatches purely on `T`'s type category: floating → `from_chars_float_advanced`, integer → `from_chars_int_advanced` (parse_number.h:521-551), selected at compile time via `from_chars_advanced_caller<TypeIx>`. Return value and error handling identical to `from_chars` (same `from_chars_result_t<UC>`, same `ec`/`ptr` semantics — see P2.4/P2.9 above). Nothing to free. Combines with the target in one harness as a strictly more general replacement: a harness can build `parse_options_t<UC>` from raw fuzz bytes (format bitmask, decimal_point char, base) and call `from_chars_advanced` directly instead of (or in addition to) `from_chars`, to reach `chars_format` combinations `from_chars` cannot express in a single call (arbitrary bit combinations, custom decimal_point, or integer base together with its own `chars_format` bits).
**Evidence:**
> include/fast_float/fast_float.h:43-46 (declaration), parse_number.h:543-551 (definition, dispatch on `is_supported_float_type<T>`/`is_supported_integer_type<T>`).
> float_common.h:71-84: `parse_options_t<UC>` definition and `using parse_options = parse_options_t<char>;`.
> tests/example_comma_test.cpp:10-12: `fast_float::parse_options options{fast_float::chars_format::general, ','}; auto answer = fast_float::from_chars_advanced(input.data(), input.data()+input.size(), result, options);`.
> tests/fortran.cpp:12-16, tests/json_fmt.cpp:10-14: show `chars_format::fortran` / `chars_format::json | chars_format::allow_leading_plus` bit combinations only reachable through `from_chars_advanced` (the plain `from_chars` only accepts a single `chars_format` and builds `parse_options_t` with default `decimal_point='.'`, `base=10` — parse_number.h:188).

### Entry: fast_float::from_chars_float_advanced (core orchestrator, also usable directly as its own "entry" per the Logic Group description)
**Claim:** `from_chars_float_advanced(UC const *first, UC const *last, T &value, parse_options_t<UC> options) noexcept` (parse_number.h:314-399). Requires the same preconditions as `from_chars`/`from_chars_advanced` (T must satisfy `is_supported_float_type`, UC must satisfy `is_supported_char_type`, enforced by `static_assert` at parse_number.h:319-322). It is reached by `from_chars_advanced` only when `T` is a floating type (`from_chars_advanced_caller<1>::call`, parse_number.h:525-532); it is *not* reachable for integer T. Internally: skips whitespace if `skip_white_space` set (327-331), rejects empty range (332-336), determines `bjf` (basic_json_fmt) to select the `parse_number_string<true/false,UC>` instantiation (337, 343-345), falls back to `detail::parse_infnan` on invalid numeric syntax unless `no_infnan` is set (346-354), and chooses between the Clinger fast path (`clinger_fast_path_impl`, 376-378), the Eisel-Lemire path (`compute_float`, 380-388), and `parse_number_slow_path` (bignum path, 370-371 and 386-388) depending on digit count/`too_many_digits` and whether Eisel-Lemire resolved (`am.power2 < 0`). Return/cleanup identical to target. In one harness, calling `from_chars_float_advanced` directly (bypassing `from_chars`/`from_chars_advanced`) lets a fuzzer pass raw `parse_options_t<UC>` bit patterns straight into the float-specific core without the dispatcher's type-based routing, directly exercising the slow/bignum vs fast-path branches by controlling digit-count and exponent magnitude in the input bytes.
**Evidence:**
> parse_number.h:314-322 (signature + static_asserts), 343-345 (bjf-based dispatch to `parse_number_string`), 370-371/386-388 (`parse_number_slow_path` calls), 376-378 (`clinger_fast_path_impl`), 380-381 (`compute_float<binary_format<T>>`).
> parse_number.h:525-532 (`from_chars_advanced_caller<1>::call` invokes it) — confirms it is only reached for float T from the public dispatcher.

### Entry: fast_float::parse_int_string (core for the integer `from_chars` overload)
**Claim:** `parse_int_string(UC const *p, UC const *pend, T &value, parse_options_t<UC> options)` (ascii_number.h:562-565, `fastfloat_really_inline`, no `noexcept` keyword at declaration but behaves as a leaf numeric scanner with no exceptions/allocations). Reached only from `from_chars_int_advanced` (parse_number.h:518, itself reached from `from_chars_advanced_caller<2>`, parse_number.h:534-541, which is selected only for `is_supported_integer_type<T>`), and from the public integer `from_chars(first,last,value,int base=10)` overload indirectly through `from_chars_advanced` (parse_number.h:410-412). Preconditions enforced by its caller `from_chars_int_advanced` before `parse_int_string` runs: `first != last` and `2 <= base <= 36` (parse_number.h:507-516) — `parse_int_string` itself does not re-check `base` range or emptiness, so it must not be called directly with an out-of-range base or empty span. Inside, it rejects a leading `-` for unsigned `T` (ascii_number.h:578-585), accepts `+`/`-` per `allow_leading_plus` (586-589), skips leading zeros (593-599), and has specialized fast loops for `uint8_t`/`uint16_t` base-10 parsing (601+) plus general base-N digit loops (not fully quoted here) with overflow detection reporting `std::errc::result_out_of_range` (e.g. 673, 684, 718, 727, 778, 801, 809...). Return value handling and cleanup identical pattern to target (`from_chars_result_t<UC>{ptr, ec}`, no allocation, nothing to free). In one harness, `parse_int_string` is reached simply by calling the public integer `from_chars(first,last,int_value,base)` overload with fuzzed `base` in [2,36] and signed/unsigned `T`, to independently exercise `ascii_number.h`'s digit-scanning/overflow logic shared conceptually with the float digit scanner (`parse_number_string`), without engaging any of the float bignum/Eisel-Lemire machinery.
**Evidence:**
> ascii_number.h:562-565 (signature), 578-585 (unsigned+negative rejection), 586-589 (sign handling with allow_leading_plus), 593-599 (leading zero skip), 601-691 (uint8_t/uint16_t fast paths with explicit `result_out_of_range` on overflow).
> parse_number.h:493-519 (`from_chars_int_advanced`: whitespace skip, `first==last || base<2 || base>36` → `invalid_argument` guard at line 512, then calls `parse_int_string` at 518).
> parse_number.h:401-413 (public integer `from_chars` overload: `static_assert(is_supported_integer_type<T>::value...)`, builds `options.base = base;`, forwards to `from_chars_advanced`).
> tests/fast_int.cpp:1495,1638,1671: calls like `fast_float::from_chars(s.data(), s.data()+s.size(), result, base)` with varying `base`, exercising this exact chain end-to-end.

## P2.6 (continued) API Existence for Core Functions Not Yet Directly Quoted
**Claim:** `compute_float`, `compute_error`, and the `digit_comp` family (`digit_comp`, `positive_digit_comp`, `negative_digit_comp`) and `detail::parse_number_string` all exist in the codebase, invoked transitively from `from_chars_float_advanced`/`parse_number_slow_path`; they are internal `core` functions not meant to be called directly by a harness (no public declaration in `fast_float.h`), consistent with the Logic Group description that only `from_chars`, `from_chars_advanced`, `from_chars_float_advanced`, and `parse_int_string` are "Entries".
**Evidence:**
> include/fast_float/parse_number.h:5-6: `#include "decimal_to_binary.h"` and `#include "digit_comparison.h"` — the headers declaring `compute_float`/`compute_error` and `digit_comp`/`positive_digit_comp`/`negative_digit_comp` respectively.
> include/fast_float/parse_number.h:380-381, 423-424: `compute_float<binary_format<T>>(pns.exponent, pns.mantissa)` call sites.
> include/fast_float/ascii_number.h:319+ : `parse_number_string` template definition (used by `from_chars_float_advanced`, parse_number.h:343-345, and by `parse_number_slow_path`, parse_number.h:308-310).
> Not found in `fast_float.h` (the public umbrella header, 91 lines total, listed fully above): no declaration of `compute_float`, `compute_error`, `digit_comp`, or `parse_number_string` there — confirming they are internal/core, not part of the public API surface a harness should call directly (only reachable transitively through the Entries).

## Build Notes
**Claim:** The project ships its own OSS-Fuzz-style fuzzer at `fuzz/from_chars.cc`, compiled as C++ (header-only library, included via `-I $SRC/fast_float/include`) and linked with `$LIB_FUZZING_ENGINE`; no other libraries are needed since fast_float is header-only. In the current checked-out tree, `fuzz/from_chars.cc` is a stub (empty harness) — the real target-calling logic must be (re)written by the harness generator, but the build wiring itself is authoritative evidence of how to compile/link a new harness.
**Evidence:**
> ${QF_WORK}/src/fast_float-c1/fast_float/fuzz/build.sh:1-8:
> ```
> $CXX $CFLAGS $CXXFLAGS -I $SRC/fast_float/include -c $SRC/fast_float/fuzz/from_chars.cc -o from_chars.o
> $CXX $CFLAGS $CXXFLAGS $LIB_FUZZING_ENGINE from_chars.o -o $OUT/from_chars
> ```
> ${QF_WORK}/ossfuzz_view/fast_float-c1/Dockerfile:17-20: `FROM gcr.io/oss-fuzz-base/base-builder`, clones `fastfloat/fast_float.git`, `WORKDIR fast_float`, copies `run_tests.sh build.sh` to `$SRC/`.
> ${QF_WORK}/ossfuzz_view/fast_float-c1/build.sh:19-20: `sh ./fuzz/build.sh` — the OSS-Fuzz-level `build.sh` just delegates to the in-repo `fuzz/build.sh` shown above.
> ${QF_WORK}/src/fast_float-c1/fast_float/fuzz/from_chars.cc:1-13: current content is a stub `LLVMFuzzerTestOneInput` that ignores `data`/`size` and returns 0 ("Gold harness replaced by an empty stub for baseline generation") — confirms the harness file is the one to (re)generate; it must `#include "fast_float/fast_float.h"` (the umbrella header at include/fast_float/fast_float.h) given the `-I $SRC/fast_float/include` include path, and is compiled as C++ (no `extern "C"` needed around fast_float calls themselves, though `LLVMFuzzerTestOneInput` itself is commonly declared `extern "C"` per the stub's own `QF_EXTERN` macro, from_chars.cc:4-9).
> fuzz/build.sh:10-11: additionally builds the project's CMake unit tests (`cmake -DFASTFLOAT_TEST=ON ...; make`) in the same build step — not part of the fuzzer binary, informational only.
## target: double_conversion::StringToDoubleConverter::StringToDouble

### Overview
`StringToDouble(const char* buffer, int length, int* processed_characters_count) const` is declared at `double-conversion/string-to-double.h:186-188`. It is a thin wrapper that forwards to the private templated core:

> double-conversion/string-to-double.cc:814-819:
> ```
> double StringToDoubleConverter::StringToDouble(
>     const char* buffer,
>     int length,
>     int* processed_characters_count) const {
>   return StringToIeee(buffer, length, true, processed_characters_count);
> }
> ```

The uc16 overload (string-to-double.cc:822-827) does the same with a 16-bit iterator. `StringToFloat` (string-to-double.cc:830-845) calls the same `StringToIeee<Iterator>` with `read_as_double=false` and casts the double result to float. `StringTo<double>`/`StringTo<float>` (string-to-double.cc:848-881) are explicit template specializations that just dispatch to `StringToDouble`/`StringToFloat` for both `char*` and `uc16*`.

### P2.1 Init Sequence
**Claim:** A `StringToDoubleConverter` object must be constructed first (via its only constructor, which takes flags/empty/junk values/symbols/separator); there is no other library-wide init. `StringToDouble` is a `const` member function, so the same converter instance can be reused for arbitrarily many calls without re-initialization.
**Evidence:**
> double-conversion/string-to-double.h:167-179: constructor `StringToDoubleConverter(int flags, double empty_string_value, double junk_string_value, const char* infinity_symbol, const char* nan_symbol, uc16 separator = kNoSeparator)` initializes `flags_, empty_string_value_, junk_string_value_, infinity_symbol_, nan_symbol_, separator_` — all `const` members.
> double-conversion/string-to-double.h:186-188: `StringToDouble(...) const` — const method, no mutable state, safe to call repeatedly on one instance.
> test/cctest/test-conversions.cc:2141-2151 (`StrToD16`) and :2181-2205 (`StrToD`) always construct a local `StringToDoubleConverter` right before calling `StringToDouble`, demonstrating the required order: construct converter → call StringToDouble.

### P2.2 Parameter Construction
**Claim:** `buffer` is a raw pointer to `length` characters (char or uc16) that need not be NUL-terminated; `length` is the exact element count to scan (callers typically use `strlen()` for C strings or an explicit array size for uc16); `processed_characters_count` must be a valid non-null `int*` — the function unconditionally writes to it (`*processed_characters_count = 0;` at function entry).
**Evidence:**
> double-conversion/string-to-double.cc:481-489 (`StringToIeee`):
> ```
> Iterator current = input;
> Iterator end = input + length;
> *processed_characters_count = 0;
> ```
> test/cctest/test-conversions.cc:2186-2188:
> ```
> int len = static_cast<int>(strlen(str));
> double result = converter.StringToDouble(str, len, processed_characters_count);
> ```
> test/cctest/test-conversions.cc:2158-2171 builds a fixed-size uc16 array and passes its length explicitly, confirming `length` is caller-supplied and not derived from a terminator inside the function.
> No null-check on `processed_characters_count` exists in the body (string-to-double.cc:481-489) — passing NULL is undefined behaviour; harness must always supply a valid `int` variable address.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is Create (constructor) → (optionally call many times) StringToDouble/StringToFloat/StringTo → destroy (implicit, stack object, no explicit free/close call exists — the class owns no heap resources beyond the raw `infinity_symbol_`/`nan_symbol_` pointers it does NOT take ownership of).
**Evidence:**
> double-conversion/string-to-double.h:219-225: all members are `const` scalars or raw `const char*` pointers copied by value — the class does not allocate or free memory itself.
> double-conversion/string-to-double.h:101-103: "infinity_symbol and nan_symbol are strings that are used to detect inputs... They can be null, in which case they are ignored." — caller owns and must keep these strings alive for the converter's lifetime (no copy is made, only the pointer is stored, string-to-double.h:223-224).
> No destructor is declared in string-to-double.h:35-234, and `DOUBLE_CONVERSION_DISALLOW_IMPLICIT_CONSTRUCTORS` (string-to-double.h:233) only disables copy/assignment, not destruction — a plain stack-allocated converter is fine.

### P2.4 Return Value Handling
**Claim:** The `double` return value is one of: `empty_string_value_` (empty/whitespace-only input), `junk_string_value_` (malformed input, e.g. default caller-supplied NaN), `±0.0`/`±Infinity`/NaN (special literal), or the parsed numeric value. Callers must inspect `*processed_characters_count` (and typically compare it against `length`) to know whether the whole buffer was consumed or trailing junk was left; the return value alone does not indicate success/failure unless `junk_string_value_` is a sentinel chosen to be otherwise unreachable (commonly `Double::NaN()`, which the caller then detects with `std::isnan`/bit comparison since NaN != NaN).
**Evidence:**
> double-conversion/string-to-double.cc:505: `if (current == end) return empty_string_value_;`
> double-conversion/string-to-double.cc:514: `return junk_string_value_;` (multiple similar `return junk_string_value_;` sites, e.g. lines 534, 542, 546, 549, 560, 564, 567, 589, 596, 654, 658...)
> test/cctest/test-conversions.cc:2141-2151: `StrToD16` returns the parsed double and also sets `*processed_all = (length == *processed_characters_count);` — this is how real callers determine "fully consumed" vs "trailing junk left".
> test/cctest/test-conversions.cc:2184-2186: converter constructed with `Double::NaN()` as junk_string_value, confirming the NaN-as-error-sentinel idiom used throughout the test-suite (the project's own primary caller).

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup/free call exists for `StringToDoubleConverter` or for the buffers passed to `StringToDouble`. If the harness heap-allocates the input buffer, `infinity_symbol`, or `nan_symbol` strings, it alone is responsible for freeing them after the call(s) complete — the converter does not take ownership or free anything.
**Evidence:**
> double-conversion/string-to-double.h:219-225: only raw pointer members, no owned heap allocation.
> double-conversion/string-to-double.cc: no `delete`/`free` anywhere in the file (searched; class has no destructor logic, only scalar members).

### P2.6 API Existence
**Claim:** All reported functions exist in the current codebase at the given paths.
**Evidence:**
> double-conversion/string-to-double.h:167-179 — constructor exists.
> double-conversion/string-to-double.h:186-217 — `StringToDouble` (char*/uc16*), `StringToFloat` (char*/uc16*), `StringTo<T>` (char*/uc16*) all exist as declared.
> double-conversion/string-to-double.cc:814-881 — all definitions exist and match declarations.
> double-conversion/string-to-double.cc:481 — private `StringToIeee` template core exists (`double-conversion/string-to-double.h:228-231` private declaration).
> double-conversion/string-to-double.cc:280 `RadixStringToIeee`, :207 `Advance`, :233 `IsHexFloatString`, :77 `ConsumeSubStringImpl`/`ConsumeSubString` all exist in this file (internal, not part of the public entry surface).
> double-conversion/strtod.h:46 `StrtodTrimmed`, :51 `StrtofTrimmed` exist (internal core, invoked from `StringToIeee` at string-to-double.cc:805,807).

### P2.7 Co-call Constraints
**Claim:** There is no lock/unlock or begin/commit pairing. The only "pairing" is conceptual: construct-once-per-flag-configuration, then call any of `StringToDouble`/`StringToFloat`/`StringTo<T>` any number of times on that instance. Different flag/symbol/separator combinations require separate converter instances (fields are `const`, set only at construction).
**Evidence:**
> double-conversion/string-to-double.h:219-225 (`const int flags_` etc. — immutable after construction, so changing behaviour requires a new object).
> test/cctest/test-conversions.cc:2208-2225 and :2569-2572, :3032, :3245-3248 each construct a fresh `StringToDoubleConverter` with a new `flags` bit combination before exercising a new code path — confirms one converter per configuration, re-used across many `StringToDouble` calls within that configuration (e.g. multiple `CHECK_EQ(..., StrToD(...))` calls share one `flags` value per TEST block).

### P2.8 Prerequisite State
**Claim:** No external context, global state, connections, or environment setup is required beyond the converter object itself and the input buffer memory being valid and readable for `length` elements. `infinity_symbol`/`nan_symbol`, if non-null, must be valid NUL-terminated C strings that satisfy documented restrictions (must not start with '+'/'-', must not start with digits, must not share the same first character as each other).
**Evidence:**
> double-conversion/string-to-double.h:104-112: "The conversion routine first reads any possible signs... This means that the following restrictions apply to special-value symbols: - they must not start with signs..., - they must not have the same first character., - they must not start with digits."
> double-conversion/string-to-double.cc:539-573: `ConsumeFirstCharacter`/`ConsumeSubString` are invoked directly on `infinity_symbol_`/`nan_symbol_` with no additional validation of these restrictions — violating them is undefined/unspecified behaviour from the API's perspective, so a harness constructing random symbol strings should keep them well-formed per spec (or accept the resulting behavior is "whatever the code does", not a contract violation of the library itself since it is documented caller responsibility).
> double-conversion/string-to-double.h:114-118 — if a `separator` other than `kNoSeparator` is used, it "is not allowed to appear in the exponent... not allowed to lead or trail the number... not allowed to appear twice next to each other" — again caller-input dependent, exercised by the core's `Advance()` function (string-to-double.cc:206-223).

### P2.9 Result Usage
**Claim:** The project's own real "caller" of `StringToDouble`/`StringToFloat`/`StringTo<T>` is the test-suite (test-conversions.cc) and the OSS-Fuzz fuzzer target; there is no downstream project API in double-conversion itself that consumes the returned `double`/`float` object further (it is a terminal numeric value, not a handle). The two outputs — the return value and `*processed_characters_count` — are read directly by the caller's own logic (comparisons, printing, deciding whether trailing junk exists), not passed into another library API.
**Evidence:**
> test/cctest/test-conversions.cc:2147-2149: `double result = converter.StringToDouble(str16, length, processed_characters_count); *processed_all = (length == *processed_characters_count); return result;` — result and processed count are both consumed directly by the test helper's own return/out-params, not fed into another double-conversion API.
> ossfuzz_view/double-conversion-c1/string_to_double_fuzzer.cc:1-17 — current harness in the OSS-Fuzz tree is a stub (`qf_target_0`/`qf_target_1` keep-alive aliases only, `LLVMFuzzerTestOneInput` does nothing); it does NOT call `StringToDouble` meaningfully, confirming the real call pattern must be sourced from the unit tests instead (see P2.1-9 evidence above). This is noted explicitly: "Gold harness replaced by an empty stub for baseline generation."
> No call to `DoubleToStringConverter` (the inverse direction, double-conversion/double-to-string.h) or any other round-trip API was found chained directly onto a `StringToDouble` call-site in test-conversions.cc within the inspected regions — the double is consumed by `CHECK_EQ` test assertions only.

### P2.10 Behaviour Controls
**Claim:** Behaviour is controlled by (a) the `flags` bit-mask passed to the constructor, (b) `empty_string_value`/`junk_string_value` sentinels, (c) non-null `infinity_symbol`/`nan_symbol`, (d) `separator`, and (e) the `read_as_double` bool hard-coded by which entry (`StringToDouble` vs `StringToFloat`) is called. These select different branches inside `StringToIeee`.
**Evidence:**
> double-conversion/string-to-double.h:39-50 (`enum Flags`): `ALLOW_HEX=1, ALLOW_OCTALS=2, ALLOW_TRAILING_JUNK=4, ALLOW_LEADING_SPACES=8, ALLOW_TRAILING_SPACES=16, ALLOW_SPACES_AFTER_SIGN=32, ALLOW_CASE_INSENSITIVITY=64, ALLOW_HEX_FLOATS=128`.
> double-conversion/string-to-double.cc:491-495: flags unpacked into local bools at top of `StringToIeee`: `allow_trailing_junk, allow_leading_spaces, allow_trailing_spaces, allow_spaces_after_sign, allow_case_insensitivity`.
> double-conversion/string-to-double.cc:585-615: `(flags_ & ALLOW_HEX) || (flags_ & ALLOW_HEX_FLOATS)` gates the `0x`/`0X` branch; `parse_as_hex_float = (flags_ & ALLOW_HEX_FLOATS) && IsHexFloatString(...)` further selects hex-integer vs hex-float sub-path inside `RadixStringToIeee<4>`.
> double-conversion/string-to-double.cc:626: `bool octal = leading_zero && (flags_ & ALLOW_OCTALS) != 0;` gates octal parsing.
> double-conversion/string-to-double.cc:539-573: non-null `infinity_symbol_`/`nan_symbol_` activate `ConsumeFirstCharacter`/`ConsumeSubString` special-value branches; null skips them entirely (`if (infinity_symbol_ != DOUBLE_CONVERSION_NULLPTR) { ... }`).
> double-conversion/string-to-double.cc:805-808: `if (read_as_double) { converted = StrtodTrimmed(...); } else { converted = StrtofTrimmed(...); }` — selected by which public entry (`StringToDouble` passes `true`, `StringToFloat` passes `false`; string-to-double.cc:818,834).
> double-conversion/string-to-double.cc:207-223 (`Advance`): `separator == kNoSeparator` short-circuits to plain `++it`; a real separator activates the look-ahead digit-group-skipping logic.

### P2.11 Input Expectations
**Claim:** The input is a raw character buffer of exactly `length` elements (no implicit NUL-termination requirement, since `end = input + length` is used, not a strlen scan); optional leading/trailing whitespace (only if the corresponding flag is set); optional sign; optional special-value literal (`infinity_symbol`/`nan_symbol` if configured); decimal / (if `ALLOW_HEX` or `ALLOW_HEX_FLOATS`) hex-prefixed `0x...` / (if `ALLOW_OCTALS`) octal digit sequence; up to `kMaxSignificantDigits` (=772) significant digits are copied into a fixed stack buffer, extra digits become "insignificant" and only affect the exponent (`nonzero_digit_dropped`); malformed/truncated inputs (`"0x"`, `"123e"`, `"--1"`) are explicitly handled per the documented examples and either produce `junk_string_value_` or ignore trailing junk depending on `ALLOW_TRAILING_JUNK`.
**Evidence:**
> double-conversion/string-to-double.cc:486-487: `Iterator current = input; Iterator end = input + length;` — length-bounded, not NUL-terminator-bounded.
> double-conversion/string-to-double.cc:126: `const int kMaxSignificantDigits = 772;`
> double-conversion/string-to-double.cc:629-647: fixed `char buffer[kMaxSignificantDigits + 10]` fed by the digit-copy loop; digits beyond the cap go to `insignificant_digits`/`nonzero_digit_dropped` instead of the buffer (bounds-checked via `DOUBLE_CONVERSION_ASSERT(buffer_pos < kBufferSize)` at line 637).
> double-conversion/string-to-double.h:120-166 doc-comment block gives exact expected outputs for edge cases such as `"0x"` → junk_string_value, `"--123.45"` → junk_string_value, `"123e"` → trailing junk ignored (returns 123.0), matching the code at string-to-double.cc:589 (`if (current == end) return junk_string_value_; // "0x"`).
> double-conversion/string-to-double.cc:589: explicit `// "0x"` truncation check — guards against reading past `end` after consuming the 'x'.

## Build notes
**Claim:** The fuzzer/harness is built as C++ (uses C++11), includes the target header via `-I${SRC}/double-conversion/double-conversion` (so `#include "string-to-double.h"` resolves), and links against the static library `libdouble-conversion.a` built by CMake/Ninja plus `${LIB_FUZZING_ENGINE}`.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/double-conversion-c1/build.sh:19-23: `cmake -GNinja -DBUILD_TESTING=ON ${SRC}/double-conversion/ && ninja` builds the library (produces `libdouble-conversion.a`).
> ${QF_WORK}/ossfuzz_view/double-conversion-c1/build.sh:27-31:
> ```
> ${CXX} ${CXXFLAGS} -std=c++11 -I${SRC}/double-conversion/double-conversion \
>     -c ${SRC}/${fuzzer}.cc -o ${fuzzer}.o
> ${CXX} ${CXXFLAGS} -std=c++11 ${fuzzer}.o \
>     -o ${OUT}/${fuzzer} "${LIB_FUZZING_ENGINE}" libdouble-conversion.a
> ```
> ${QF_WORK}/ossfuzz_view/double-conversion-c1/Dockerfile:24-28: clones `double-conversion` repo to `$SRC/double-conversion`, copies `*.cc` fuzzer sources to `$SRC/`.
> The public header the harness must include is `double-conversion/string-to-double.h` (relative to the `-I.../double-conversion` include path, i.e. `#include "string-to-double.h"` as done internally, or `#include "double-conversion/string-to-double.h"` depending on include-path depth used by the actual fuzzer source — the current stub at `string_to_double_fuzzer.cc` does not include the header at all (empty stub noted at line 1), so the real include form must be taken from the project's canonical fuzzer (not present in this checkout) or from `test/cctest/test-conversions.cc:1` pattern — check that file's own `#include` line for the exact form expected when writing a new harness.

### Build notes addendum (include path confirmation)
**Claim:** The canonical public include for `StringToDoubleConverter`/`StringToDouble` is `double-conversion/double-conversion.h` (which itself `#include`s `string-to-double.h`), included with the `-I${SRC}/double-conversion/double-conversion` compile flag used by `build.sh`. A harness should use `#include "double-conversion/double-conversion.h"` (matching the project's own test convention) or `#include "string-to-double.h"` directly (matching internal file-local includes); both resolve given the current `-I` flag.
**Evidence:**
> test/cctest/test-conversions.cc:31: `#include "double-conversion/double-conversion.h"` — the project's own real caller (test-conversions.cc) uses this form, with `double-conversion/ieee.h` and `double-conversion/utils.h` also included for `Double::NaN()` etc.
> double-conversion/double-conversion.h:31-32: `#include "string-to-double.h"` then `#include "double-to-string.h"` — confirms `double-conversion.h` is the umbrella public header that re-exports `StringToDoubleConverter`.
> ${QF_WORK}/ossfuzz_view/double-conversion-c1/build.sh:27: `-I${SRC}/double-conversion/double-conversion` — this is the include root; with it, both `#include "double-conversion/double-conversion.h"` (from `$SRC/double-conversion`, one level up) — note: actual effective include root must be verified against directory layout (`$SRC/double-conversion/double-conversion/string-to-double.h` is the physical file per this project's Glob result `double-conversion/string-to-double.h`), so `#include "string-to-double.h"` (bare) matches the `-I.../double-conversion` flag directly, while `#include "double-conversion/double-conversion.h"` would require `-I${SRC}/double-conversion` (one directory higher) — the harness generator should match whichever is used elsewhere in the final harness's actual `-I` flags set by the pipeline's build.sh, favoring the bare form `"string-to-double.h"` given the flag shown in this project's build.sh line 27.
> ${QF_WORK}/ossfuzz_view/double-conversion-c1/Dockerfile:24-26: library cloned to `$SRC/double-conversion`; CMake build in build.sh:22 points at `${SRC}/double-conversion/` — physical header path is `${SRC}/double-conversion/double-conversion/string-to-double.h`.
> Linked library: static archive `libdouble-conversion.a` produced by the CMake/Ninja build (build.sh:19-23), linked alongside `${LIB_FUZZING_ENGINE}` (build.sh:30-31) — no other third-party libraries required.
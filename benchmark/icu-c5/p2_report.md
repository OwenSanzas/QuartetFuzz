## target: icu::UnicodeString(const char *codepageData, int32_t dataLength, const char *codepage)

### P2.1 Init Sequence
**Claim:** No explicit ICU global "init" call is required before constructing a `UnicodeString` from codepage bytes (ICU initializes internal caches/converters lazily on first use). The only "setup" step belonging to this feature is supplying a codepage name — either leave it `nullptr` (default converter, ICU resolves via `ucnv_getDefaultName()`), pass `""` (invariant-char fast path, no converter object at all), or pass a real converter name that will internally be opened via `ucnv_open()`. If the harness wants to reuse a pre-opened `UConverter*` across iterations it must call `ucnv_open()` itself beforehand and use the separate `UnicodeString(const char*, int32_t, UConverter*, UErrorCode&)` overload (see below) instead of the 3-arg `codepage` overload.
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:70-77: `UnicodeString::UnicodeString(const char *codepageData, int32_t dataLength, const char *codepage) { fUnion.fFields.fLengthAndFlags = kShortString; if (codepageData != nullptr) { doCodepageCreate(codepageData, dataLength, codepage); } }` — no other call precedes it.
> icu4c/source/common/unistr_cnv.cpp:320-338 (`doCodepageCreate`): `if (codepage == nullptr) { ... converter = u_getDefaultConverter(&status); } else if (*codepage == 0) { /* invariant chars, no converter */ } else { converter = ucnv_open(codepage, &status); }` — converter creation is internal to the target; the caller supplies only the name string.

### P2.2 Parameter Construction
**Claim:** `codepageData` must point to a buffer of at least `dataLength` readable bytes (may be any fuzz-controlled bytes, embedded NULs allowed since length is explicit, not strlen-based). `dataLength` is `int32_t`: `0` and negative values other than `-1` are treated as "nothing to convert" (no-op, object stays default-constructed), `-1` means "NUL-terminated, compute via `strlen`". `codepage` is a `const char*`: `nullptr` = default platform/ICU converter, `""` = invariant-character fast path (bypasses `ucnv_open` entirely), any other string is passed verbatim to `ucnv_open()` which validates/resolves aliases internally.
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:306-312 (`doCodepageCreate`): `if (codepageData == nullptr || dataLength == 0 || dataLength < -1) { return; } if(dataLength == -1) { dataLength = static_cast<int32_t>(uprv_strlen(codepageData)); }`
> unicode/unistr.h:3496-3511 (constructor doc): "@param dataLength The number of bytes... @param codepage... value 0 ... indicates platform's default codepage. If `codepage` is an empty string (""), then a simple conversion is performed on the codepage-invariant subset."

### P2.3 Object Lifecycle
**Claim:** `UnicodeString` is a stack-constructible value type: construct via the codepage constructor → use as a UTF-16 string (append, compare, extract, pass to other ICU APIs) → destroy via normal C++ scope exit (its destructor frees any heap-allocated internal buffer). There is no separate "close"/"destroy" API call needed by the caller; `~UnicodeString()` handles it. No converter object is owned by the resulting `UnicodeString` — any converter opened internally (`ucnv_open`) is closed before the constructor returns (see P2.5).
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:352-357: `if (codepage == nullptr) { u_releaseDefaultConverter(converter); } else { ucnv_close(converter); }` — converter lifecycle is fully contained inside the constructor call, not exposed to caller.
> unicode/unistr.h: `UnicodeString` is a `Replaceable` value class with public copy/move/destructor (standard RAII); no explicit `close()`/`destroy()` method exists for it (searched unistr.h, no match).

### P2.4 Return Value Handling
**Claim:** The 3-arg codepage constructor has no return value/error code parameter — failure (bad codepage name, allocation failure during buffer growth) is signaled by the object becoming "bogus". Callers must check `isBogus()` after construction before using the string; a bogus `UnicodeString` has `length()==0` and `getBuffer()==nullptr` semantics are undefined for further mutation until reset.
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:340-344: `if(U_FAILURE(status)) { setToBogus(); return; }` (invalid codepage name case)
> icu4c/source/common/unistr_cnv.cpp:348-350: `doCodepageCreate(codepageData, dataLength, converter, status); if(U_FAILURE(status)) { setToBogus(); }` (conversion failure case)
> icu4c/source/test/intltest/ustrtest.cpp:1472-1481: real caller pattern — `if (test1.isBogus() || test2.isBogus() || test3.isBogus()) { errln(...); }` / `if(!test3.isBogus()) { errln("A bogus string returned false for isBogus()!"); }`

### P2.5 Cleanup Sequence
**Claim:** Cleanup is two-layered. (a) Internal: the converter opened by `doCodepageCreate` (default or named) is released/closed *inside* the constructor, on both success and failure paths, before the constructor returns — the caller never sees or owns this converter when using the 3-arg `codepage` constructor. (b) Caller-level: nothing beyond letting the `UnicodeString` go out of scope / be destructed (its destructor frees the internal `char16_t` array if heap-allocated via `cloneArrayIfNeeded`). If the harness instead uses the `UConverter*` overload, the harness itself owns the converter and must `ucnv_close()` it after use.
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:346-357: converter release happens unconditionally right after `doCodepageCreate(...)` regardless of status, inside the 3-arg-codepage `doCodepageCreate` function.
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:41-45: harness-owned converter example: `deleted_unique_ptr<UConverter> converter(ucnv_open(converter_name, &status), &ucnv_close);` — explicit caller-managed close via unique_ptr deleter, used with the `UConverter*`-based `extract` overload (not the codepage-name constructor).

### P2.6 API Existence
**Claim:** All functions mentioned exist in the current codebase at the cited locations.
**Evidence:**
> `UnicodeString(const char*, int32_t, const char*)`: unicode/unistr.h:3511; defined unistr_cnv.cpp:70-77
> `UnicodeString(const char*, int32_t, UConverter*, UErrorCode&)`: defined unistr_cnv.cpp:79-112
> `UnicodeString::extract(int32_t,int32_t,char*,uint32_t,const char*)`: unistr_cnv.cpp:131-204
> `UnicodeString::extract(char*,int32_t,UConverter*,UErrorCode&)`: unistr_cnv.cpp:206-247
> `UnicodeString::doCodepageCreate` (2 overloads): unistr_cnv.cpp:301-358 and 360-418
> `UnicodeString::doExtract`: unistr_cnv.cpp:249-299
> `ucnv_open`: unicode/ucnv.h:365-366
> `ucnv_close`: unicode/ucnv.h:581-582
> `u_getDefaultConverter` / `u_releaseDefaultConverter`: common/ustr_cnv.h:31-40
> `ucnv_getDefaultName`: unicode/ucnv.h:1885-1886

### P2.7 Co-call Constraints
**Claim:** The paired API relevant to this feature is `ucnv_open` / `ucnv_close` — any harness-opened `UConverter*` (used with the `UConverter*`-taking constructor/`extract` overload) must be closed exactly once with `ucnv_close` after last use. When the harness instead uses the plain `codepage`-name constructor/`extract(..., codepage)` overload, this pairing is handled internally and invisible to the caller (no action needed). There is no explicit "begin/commit" pairing for `UnicodeString` itself; construction and destruction (C++ RAII) is the only lifecycle pairing.
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:191-201 (`extract` with codepage name): `converter = ucnv_open(codepage, &status); ... ucnv_close(converter);` — open/close always paired within the same call.
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:41-50: harness pairs `ucnv_open` with `ucnv_close` via RAII (`deleted_unique_ptr`).
> unicode/ucnv.h:576 ("@param converter the converter object to be deleted") confirms `ucnv_close` is the required counterpart to `ucnv_open`.

### P2.8 Prerequisite State
**Claim:** No connection/context object must exist before calling the target. The only implicit prerequisite is ICU's data (converter tables, codepage alias table) being loaded — this happens automatically from the statically linked `icudata` at first `ucnv_open`/`u_getDefaultConverter` call; the OSS-Fuzz build links `-licudata` so this is satisfied by linking alone, no explicit `u_init()` call is required or present in any fuzzer.
**Evidence:**
> ossfuzz_view/icu-c5/build.sh:53: `$LIB_FUZZING_ENGINE -licui18n -licuuc -licutu -licudata` — statically links ICU data package needed by `ucnv_open`.
> Searched icu4c/source/test/fuzzer/*.cpp — none call `u_init()` before using converters (e.g. converter_fuzzer.cpp directly calls `ucnv_open` at line 41 with no prior init call).

### P2.9 Result Usage
**Claim:** The `UnicodeString` object produced by the target is consumed in real code by: (1) `extract(...)` to re-encode it back to bytes in some codepage (round trip), (2) comparison operators (`==`, `!=`) against other `UnicodeString`s, (3) passed to `length()`/`getTerminatedBuffer()`, and (4) in tools, passed to writer functions like `write_utf8_file`.
**Evidence:**
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:37,50: result of a `UnicodeString` constructor is later consumed by `fuzzstr.extract(dest_buffer.get(), dest_buffer_size, converter.get(), status);` (note: this fuzzer constructs from `char16_t*`, not the codepage-decoding constructor, but shows the extract-consumption pattern expected for the codepage-constructed object too).
> icu4c/source/test/intltest/ustrtest.cpp:1752-1753: `s=UnicodeString(testEnumStrings[i], ""); if(... UnicodeString(pc, length, "")!=s) { errln(...); }` — codepage-constructed strings compared with `!=`/`==`.
> icu4c/source/tools/genrb/wrtxml.cpp:417-420: `write_utf8_file(out,UnicodeString(buf, bufLen, "UTF-8"));` — codepage-constructed `UnicodeString` temporary passed directly into a consumer (`write_utf8_file`).
> icu4c/source/extra/uconv/uconv.cpp:822: `UnicodeString(pos, length, "").getTerminatedBuffer()` — result consumed via `getTerminatedBuffer()`.
> icu4c/source/common/unistr_cnv.cpp:194: inside `extract`, the string's own internal buffer (`getArrayStart()+start`) is read and fed to `doExtract`/`ucnv_fromUnicode`, the decode→encode round trip mentioned in the feature description.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters/flags for the target and its API surface:
- `dataLength`: `-1` selects strlen-based length; `0` or `<-1` selects no-op; `>0` selects real conversion with buffer growth loop.
- `codepage`: `nullptr` selects default-converter path (which itself special-cases UTF-8 default via `UCNV_FAST_IS_UTF8(defaultName)` and calls `setToUTF8()` directly, bypassing `ucnv_toUnicode`); `""` selects invariant-character fast path (`u_charsToUChars`, no converter, no state machine); any other non-empty string selects `ucnv_open(codepage,&status)` → generic `ucnv_toUnicode` state-machine path, whose internal behavior (single-byte table, MBCS, UTF-8, ISO-2022 stateful, etc.) depends entirely on which converter the name resolves to.
- `extract`'s `codepage` parameter mirrors the same three-way split (`nullptr`/`""`/name) on the encode (`ucnv_fromUnicode`/`toUTF8`/invariant) side.
- `dstSize`/`destCapacity` in `extract`: `0` ⇒ only NUL write; `0x7fffffff`-or-above (`uint32_t` "unlimited") is treated specially via `U_MAX_PTR` pinning to avoid pointer wraparound; `-1` (as `int32_t` passed to `doExtract`) is the "NUL-terminate, no limit" magic value pinned via `U_MAX_PTR`.
- `UConverter*` overloads (constructor and `extract`) let the harness pick an arbitrary pre-opened converter type (single-byte, DBCS/MBCS, UTF-8 built-in, ISO-2022 stateful) and additionally control converter *state* by calling/skipping `ucnv_resetToUnicode`/`ucnv_resetFromUnicode` (constructor always resets; `extract(dest,...,cnv,...)` resets only if `cnv!=nullptr`, i.e. always since nullptr there triggers default-converter path not reset).
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:320-325: `if(UCNV_FAST_IS_UTF8(defaultName)) { setToUTF8(StringPiece(codepageData, dataLength)); return; }`
> icu4c/source/common/unistr_cnv.cpp:327-335: invariant-character branch `else if (*codepage == 0) { ... u_charsToUChars(...); setLength(dataLength); ... }`
> icu4c/source/common/unistr_cnv.cpp:152-161 (`extract`): `if(dstSize < 0x7fffffff) { capacity = ...; } else { char* targetLimit = U_MAX_PTR(target); capacity = targetLimit - target; }`
> icu4c/source/common/unistr_cnv.cpp:266-275 (`doExtract`): `if(destCapacity==0) {...} else if(destCapacity==-1) { destLimit = U_MAX_PTR(dest); destCapacity=0x7fffffff; }`
> icu4c/source/common/unistr_cnv.cpp:96-98 (constructor): `ucnv_resetToUnicode(cnv); doCodepageCreate(src, srcLength, cnv, errorCode);`
> icu4c/source/common/unistr_cnv.cpp:233-235 (`extract` cnv overload): `else { isDefaultConverter=false; ucnv_resetFromUnicode(cnv); }`

### P2.11 Input Expectations
**Claim:** `codepageData` is treated as an opaque byte array (no required header/magic/terminator) of exactly `dataLength` bytes when `dataLength>=0`, or as a C string (`strlen`-terminated, so no embedded NUL allowed to be included) when `dataLength==-1`. No upper size limit is enforced in the target itself; the internal buffer (`arraySize`) grows dynamically (`cloneArrayIfNeeded` loop on `U_BUFFER_OVERFLOW_ERROR`) to accommodate arbitrarily large inputs, bounded only by allocation failure (which sets the string bogus). The `codepage` string must be NUL-terminated (passed straight to `ucnv_open`'s `const char*` parameter) and is validated/resolved by ICU's converter alias table; unknown/invalid names cause `ucnv_open` to fail (`U_FAILURE(status)`) and the whole constructor result becomes bogus.
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:306-312: length handling as above (no header check on bytes).
> icu4c/source/common/unistr_cnv.cpp:375-417 (`doCodepageCreate` core loop): dynamic `arraySize` estimate (`US_STACKBUF_SIZE` or `dataLength + dataLength>>2`), loop re-sizing to `length() + 2*(mySourceEnd-mySource)` on `U_BUFFER_OVERFLOW_ERROR`, terminated only by `cloneArrayIfNeeded` success/failure.
> unicode/ucnv.h:372-378 (`ucnv_open` doc): "The name should be limited to the ASCII-7 alphanumerics range... resolved with the alias file"; failure path returns through `status`.
> icu4c/source/common/unistr_cnv.cpp:340-344: `ucnv_open` failure ⇒ `setToBogus(); return;` — no crash/UB, but downstream harness code must check `isBogus()`.

## Other Logic-Group Entries

### icu::UnicodeString::extract(int32_t start, int32_t length, char *target, uint32_t dstSize, const char *codepage) const
**Role:** Consumer / inverse of the target — re-encodes the UTF-16 content back to bytes via `ucnv_fromUnicode`/`doExtract` (or `toUTF8`/invariant fast path), forming the decode→encode round trip.
**Call protocol:**
- Prerequisite: a non-bogus (or even bogus, handled gracefully) `UnicodeString` already constructed (e.g. via the target). `target` must be a writable buffer of at least `dstSize` bytes if `dstSize>0` (checked: `dstSize>0 && target==nullptr` ⇒ returns 0 immediately — unistr_cnv.cpp:139-141).
- Parameters: `start`/`length` are pinned to valid range via `pinIndices` (unistr_cnv.cpp:144) so out-of-range values are safe, not UB. `codepage`: same 3-way semantics as the target (`nullptr`=default/UTF-8 fast path, `""`=invariant chars, name=`ucnv_open`).
- Return: number of bytes that *would* be written (preflight semantics per ICU convention); caller should compare against `dstSize` to detect truncation.
- Cleanup: converter opened internally is closed before return (`ucnv_close`/`u_releaseDefaultConverter`, unistr_cnv.cpp:196-201) — no caller action needed for this overload.
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:131-204 (full body, see earlier excerpt).
> icu4c/source/test/fuzzer/converter_fuzzer.cpp doesn't use this particular overload (it uses the `UConverter*` variant), but icu4c/source/tools and intltest use it in the `codepage`-string form implicitly through other extract overloads delegating to this one (unistr_cnv.cpp:118-126, the 4-arg `extract` forwards to the 5-arg one with `codepage=0`).

### icu::UnicodeString::extract(char *dest, int32_t destCapacity, UConverter *cnv, UErrorCode &errorCode) const
**Role:** Consumer variant using an explicit, harness-managed `UConverter*`; pairs naturally with the `UConverter*`-based constructor to let a harness reuse one open converter object across many iterations, avoiding repeated `ucnv_open`/`ucnv_close`.
**Call protocol:**
- Prerequisite: `errorCode` must be `U_SUCCESS` on entry (checked at top, returns 0 immediately otherwise — unistr_cnv.cpp:211-213); the `UnicodeString` must not be bogus and `destCapacity>=0` (`isBogus() || destCapacity<0 || (destCapacity>0 && dest==nullptr)` ⇒ `U_ILLEGAL_ARGUMENT_ERROR`, unistr_cnv.cpp:215-218).
- `cnv`: if `nullptr`, uses `u_getDefaultConverter(&errorCode)` and releases it itself afterward (`isDefaultConverter` flag, unistr_cnv.cpp:226-244); if non-null, resets it via `ucnv_resetFromUnicode(cnv)` before use and does **not** close/release it — the caller (harness) remains the owner and must `ucnv_close` it later.
- Return: number of bytes extracted (via `doExtract`, same preflight semantics).
**Evidence:**
> icu4c/source/common/unistr_cnv.cpp:206-247 (full body).
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:41-50: harness opens converter with `ucnv_open`, wraps it in `deleted_unique_ptr<UConverter>` with `ucnv_close` as deleter, then calls `fuzzstr.extract(dest_buffer.get(), dest_buffer_size, converter.get(), status);` — the unique_ptr destructor closes the converter when it goes out of scope, demonstrating the caller-owns-converter contract for this overload.

### icu::ucnv_open(const char *converterName, UErrorCode *err)
**Role:** Converter factory; used both internally by the target (for the `codepage`-name overload) and directly by harnesses that want the `UConverter*`-based constructor/extract overloads.
**Call protocol:**
- Parameters: `converterName` — NUL-terminated name or alias (ASCII-7), or `nullptr` to get `ucnv_getDefaultName()`'s converter. `err`: must point to a `UErrorCode` initialized to `U_ZERO_ERROR` (or at least a non-failure value) before the call; `ucnv_open` is a no-op-and-returns-nullptr if `*err` already indicates failure on entry (standard ICU convention; see the "if error code indicates a failure" docs on other ucnv_open variants).
- Return: `UConverter*` or `nullptr` on failure — **must check `U_FAILURE(*err)` / non-null return before using the pointer**.
- Must be paired with exactly one `ucnv_close()` call when the harness is done with it (see P2.7).
**Evidence:**
> unicode/ucnv.h:365-366: `U_CAPI UConverter* U_EXPORT2 ucnv_open(const char *converterName, UErrorCode *err);`
> unicode/ucnv.h:355: "@return the created Unicode converter object, or NULL if an error occurred"
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:39-45: `const char* converter_name = ucnv_getAvailableName(rnd % ucnv_countAvailable()); deleted_unique_ptr<UConverter> converter(ucnv_open(converter_name, &status), &ucnv_close); if (U_FAILURE(status)) { return 0; }` — canonical open+check+RAII-close pattern a harness should follow for the `UConverter*` overloads of the target/extract.
> icu4c/source/common/unistr_cnv.cpp:337-344 (internal use by target): `converter = ucnv_open(codepage, &status); ... if(U_FAILURE(status)) { setToBogus(); return; }`

### icu::ucnv_close(UConverter *converter)
**Role:** Releases a converter created by `ucnv_open`; required cleanup counterpart whenever the harness itself opened a converter (not needed when using the internal `codepage`-name overloads of the target/`extract`, since those self-manage their converter).
**Call protocol:**
- Must be called exactly once per successful `ucnv_open()` call that the harness owns, after all uses of the converter (e.g., after the last `UnicodeString(..., UConverter*, ...)` constructor call or `extract(..., UConverter*, ...)` call using it) are done.
- Must NOT be called on a converter obtained from `u_getDefaultConverter()` — that one is released via `u_releaseDefaultConverter()` instead (different pool/cache); mixing these up is a lifecycle bug. The target itself always matches `ucnv_open`→`ucnv_close` and `u_getDefaultConverter`→`u_releaseDefaultConverter` correctly (unistr_cnv.cpp:352-357, 103).
**Evidence:**
> unicode/ucnv.h:572-582: "Does not free up shared converter tables. @param converter the converter object to be deleted ... ucnv_close(UConverter * converter);"
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:41-42: `deleted_unique_ptr<UConverter> converter(ucnv_open(converter_name, &status), &ucnv_close);` — close is bound as the RAII deleter, guaranteeing pairing even on early return.
> icu4c/source/common/unistr_cnv.cpp:103: `u_releaseDefaultConverter(cnv);` used instead of `ucnv_close` when `cnv` came from `u_getDefaultConverter` — shows the two release APIs are NOT interchangeable.

### How these combine in one harness (for this Logic Group)
A harness exercising this feature would typically:
1. Build a `codepage` name string from part of the fuzz input (or pick `nullptr`/`""`/a fixed valid name/an `ucnv_getAvailableName(idx)` name).
2. Call the target `UnicodeString(data, dataLength, codepage)` (or one of its sibling overloads) on the remaining fuzz bytes — this exercises `doCodepageCreate`→`ucnv_open`/`u_getDefaultConverter`→`ucnv_toUnicode`→`cloneArrayIfNeeded` internally, with `ucnv_close`/`u_releaseDefaultConverter` happening automatically.
3. Check `isBogus()`— if bogus, nothing more to do (but still valid, not a bug).
4. Optionally call `extract(...)` with the same or a different codepage name (or an explicitly `ucnv_open`-ed `UConverter*`, closed by the harness with `ucnv_close` afterward) to complete the decode→encode round trip and surface overflow/size mismatches between `ucnv_toUnicode` and `ucnv_fromUnicode`.
5. Let the `UnicodeString` go out of scope (destructor frees internal buffer); explicitly `ucnv_close()` any harness-opened `UConverter*` not already released internally.
**Evidence:**
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:21-53 (whole file): shows exactly this open→construct(from UTF-16, not codepage, in this particular fuzzer)→extract→close pattern that the new harness should mirror but starting from the codepage-decoding constructor instead.

## Build notes
**Claim:** Fuzzer sources for this project are plain C++ files matching `*_fuzzer.cpp` under `icu4c/source/test/fuzzer/`, compiled individually with the project's own `CXXFLAGS` (including `-DU_CHARSET_IS_UTF8=1 -DU_USING_ICU_NAMESPACE=0 -DU_ENABLE_DYLOAD=0 -DU_USE_STRTOD_L=0`), with the target's header reachable via `-I$SRC/icu/icu4c/source/common` (where `unicode/unistr.h` and `unicode/ucnv.h` live, since both are under `icu4c/source/common/unicode/`), and linked against ICU's static libs `-licui18n -licuuc -licutu -licudata` plus `$LIB_FUZZING_ENGINE`. A single shared `locale_util.o` is precompiled and linked into every fuzzer.
**Evidence:**
> ossfuzz_view/icu-c5/build.sh:44-54:
> ```
> FUZZER_PATH=$SRC/icu/icu4c/source/test/fuzzer
> FUZZERS=$FUZZER_PATH/*_fuzzer.cpp
> for fuzzer in $FUZZERS; do
>   file=${fuzzer:${#FUZZER_PATH}+1}
>   $CXX $CXXFLAGS \
>     $fuzzer -o $OUT/${file/.cpp/} locale_util.o \
>     -I$SRC/icu/icu4c/source/common -I$SRC/icu/icu4c/source/i18n -L$WORK/icu/lib \
>     $LIB_FUZZING_ENGINE -licui18n -licuuc -licutu -licudata
> done
> ```
> ossfuzz_view/icu-c5/build.sh:24-30: `DEFINES="-DU_CHARSET_IS_UTF8=1 -DU_USING_ICU_NAMESPACE=0 -DU_ENABLE_DYLOAD=0 -DU_USE_STRTOD_L=0"` then `runConfigureICU Linux --with-library-bits=64 --with-data-packaging=static --enable-static --disable-shared` — static build, so note `U_CHARSET_IS_UTF8=1` means the `#if !U_CHARSET_IS_UTF8` constructors (unistr_cnv.cpp:42-60, the `UnicodeString(const char*)`/`(const char*,int32_t)` 2-arg forms defaulting to platform codepage) are compiled out in this build (`unistr.cpp` has an alternate definition path per the `// else see unistr.cpp` comment at unistr_cnv.cpp:59); the 3-arg `codepage`-name constructor (the target) and the `UConverter*` constructor are unconditionally compiled (no `#if` guard around unistr_cnv.cpp:62-112).
> Glob confirms `icu4c/source/common/unicode/unistr.h` and `icu4c/source/common/unicode/ucnv.h` both exist directly under the `-I$SRC/icu/icu4c/source/common` include path used by build.sh.
> icu4c/source/test/fuzzer/converter_fuzzer.cpp:15-16: `#include "unicode/unistr.h"` / `#include "unicode/ucnv.h"` — confirms the include path resolves these headers exactly as build.sh configures it.
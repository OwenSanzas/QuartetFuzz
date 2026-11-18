## target: icu::Normalizer2::normalize

### Target declaration & definition
`UnicodeString Normalizer2::normalize(const UnicodeString &src, UErrorCode &errorCode) const` is a non-virtual inline wrapper, defined inline in the header:
> icu4c/source/common/unicode/normalizer2.h:220-225:
> ```
> UnicodeString
> normalize(const UnicodeString &src, UErrorCode &errorCode) const {
>     UnicodeString result;
>     normalize(src, result, errorCode);
>     return result;
> }
> ```
It constructs an empty `UnicodeString result` and forwards to the pure-virtual
`UnicodeString & normalize(const UnicodeString &src, UnicodeString &dest, UErrorCode &errorCode) const = 0;` (normalizer2.h:239-242), which is implemented per concrete subclass in `icu4c/source/common/norm2allmodes.h:38-58` (`Normalizer2WithImpl::normalize`). That implementation:
> norm2allmodes.h:41-57:
> ```
> if(U_FAILURE(errorCode)) { dest.setToBogus(); return dest; }
> const char16_t *sArray=src.getBuffer();
> if(&dest==&src || sArray==nullptr) {
>     errorCode=U_ILLEGAL_ARGUMENT_ERROR; dest.setToBogus(); return dest;
> }
> dest.remove();
> ReorderingBuffer buffer(impl, dest);
> if(buffer.init(src.length(), errorCode)) {
>     normalize(sArray, sArray+src.length(), buffer, errorCode);
> }
> ```
This then dispatches to `Normalizer2Impl::decompose` (DecomposeNormalizer2, norm2allmodes.h:195-199: `impl.decompose(src, limit, &buffer, errorCode);`) or `Normalizer2Impl::compose` (ComposeNormalizer2, norm2allmodes.h:257-...: `impl.compose(src, limit, onlyContiguous, true, &buffer, errorCode);` — see composed class below) or FCD logic (FCDNormalizer2), depending on which singleton instance (`comp`, `decomp`, `fcd`, `fcc`) was returned by `getInstance`/`getNFxInstance`.

Because `Normalizer2::normalize(src,errorCode)` is non-virtual, every concrete instance (NFC/NFD/NFKC/NFKD/NFKC_Casefold/custom/FCD/Noop) shares this same wrapper; the behaviour differs only through which singleton object `this` points at.

### P2.1 Init Sequence
**Claim:** No library-global init call is required for the C++ Normalizer2 API itself (ICU's internal data loading happens lazily/on demand). The only thing that must happen before `normalize()` is obtaining a valid `const Normalizer2 *` instance via one of the static factory functions (`getNFCInstance`, `getNFDInstance`, `getNFKCInstance`, `getNFKDInstance`, `getNFKCCasefoldInstance`, `getNFKCSimpleCasefoldInstance`, or `getInstance(packageName,name,mode,errorCode)`), checking `U_SUCCESS(errorCode)` and a non-null return, before calling `normalize`.
**Evidence:**
> unicode/normalizer2.h:97-100 (doc on every getXXXInstance): "errorCode ... Its input value must pass the U_SUCCESS() test ... Check for U_FAILURE() on output"
> icu4c/source/common/loadednormalizer2impl.cpp:286-364: `Normalizer2::getInstance()` lazily loads data (`Norm2AllModes::createInstance`) under a `Mutex`, caches it in a `uhash`, and only on success returns `&allModes->comp/decomp/fcd/fcc`.
> icu4c/source/i18n/messageformat2_function_registry.cpp:156-173 real caller pattern:
> ```
> UErrorCode status = U_ZERO_ERROR;
> const Normalizer2* nfcNormalizer = Normalizer2::getNFCInstance(status);
> if (U_FAILURE(status)) { return s; }
> ...
> UnicodeString normalized = nfcNormalizer->normalize(s, status);
> if (U_FAILURE(status)) { return {}; }
> ```
No other initialization (no "open context" object) is needed; `errorCode` must be `U_ZERO_ERROR` (or any U_SUCCESS value) on entry to the factory call, per normalizer2.h:97-99.

### P2.2 Parameter Construction
**Claim:** `src` is any `UnicodeString` (owns/aliases a `char16_t` buffer the caller constructs, e.g. from fuzz bytes via `UnicodeString(bool isTerminated, const char16_t *text, int32_t textLength)` or `UnicodeString::fromUTF8/fromUTF32`); it is passed `const &` so ownership/lifetime is caller's. `errorCode` is a stack `UErrorCode` initialized to `U_ZERO_ERROR` before the call (checked by the callee: if already a failure code, function returns immediately without touching `src`/producing a `dest`).
**Evidence:**
> normalizer2.h:213-216 "errorCode Standard ICU error code. Its input value must pass the U_SUCCESS() test, or else the function returns immediately."
> norm2allmodes.h:42 `if(U_FAILURE(errorCode)) { dest.setToBogus(); return dest; }` — concrete check performed inside the virtual normalize.
> norm2allmodes.h:46-50: `const char16_t *sArray=src.getBuffer(); if(&dest==&src || sArray==nullptr) { errorCode=U_ILLEGAL_ARGUMENT_ERROR; ...}` — src must have a valid (non-null) buffer; for the 2-arg `normalize(src,errorCode)` wrapper `dest` is the function-local `result`, so `&dest==&src` can never happen there, but it matters for the 3-arg variant used elsewhere.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: (1) obtain singleton `const Normalizer2*` via a static factory — never constructed directly, never deleted by caller; (2) call `normalize()` (or any of the other virtual entries) any number of times, the object is immutable/stateless/thread-safe for reads; (3) no destroy step for the Normalizer2 object itself — the result `UnicodeString` (returned by value) is the only caller-owned object and is destroyed by normal C++ scoping.
**Evidence:**
> normalizer2.h:46 class doc: "Instances returned by getInstance() are singletons that must not be deleted by the caller."
> normalizer2.h:96 "Returns an unmodifiable singleton instance. Do not delete it." (repeated for every getXXXInstance, e.g. lines 96,110,124,138,155,172,187).
> normalizer2.h:91 `~Normalizer2();` destructor exists but is never invoked by client code for singleton instances (only relevant for `FilteredNormalizer2` which wraps a const Normalizer2& and is stack-allocated by the caller, see filterednormalizer2.h).

### P2.4 Return Value Handling
**Claim:** The by-value `UnicodeString` result must still be checked via `errorCode`/`U_FAILURE` — on failure the returned string is "bogus" (via `dest.setToBogus()`), not necessarily empty; callers must not use the result when `U_FAILURE(errorCode)`.
**Evidence:**
> norm2allmodes.h:42-44, 48-50: both the already-failed and illegal-argument paths call `dest.setToBogus(); return dest;` before producing any content.
> messageformat2_function_registry.cpp:169-172 real-caller check:
> ```
> UnicodeString normalized = nfcNormalizer->normalize(s, status);
> if (U_FAILURE(status)) { return {}; }
> return normalized;
> ```
> test/intltest/tstnorm.cpp:1755-1762: pattern `result = n2->normalize(sh, errorCode); assertTrue(... result.isEmpty());` — callers then inspect `result` only after confirming no error (IcuTestErrorCode wrapper logs/asserts on failures upstream at getInstance time, line 1748).

### P2.5 Cleanup Sequence
**Claim:** Nothing to free for the `Normalizer2*` singleton (never owned by caller). Only the caller's own `UnicodeString` locals (src string if heap/dynamically built, result string) are destroyed by normal scope exit / stack unwind — no explicit close/free API exists or is needed on success or error paths.
**Evidence:**
> normalizer2.h:46,96 (see above) — no caller-side delete permitted on the Normalizer2*.
> No `close`/`delete norm2`/`Normalizer2::~Normalizer2()` caller-site call found in any of common/i18n call sites inspected (normalizer2.cpp, loadednormalizer2impl.cpp, nortrans.cpp, messageformat2_function_registry.cpp, usearch.cpp, uspoof.cpp, collationruleparser.cpp, nfrule.cpp) — confirmed via grep of `getNFCInstance|getNFDInstance|getNFKCInstance` call sites (see tool output), none paired with a delete of the returned pointer.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase, verified by direct reads:
**Evidence:**
> unicode/normalizer2.h:220-225 `normalize(const UnicodeString&, UErrorCode&) const` (target, inline, exists)
> unicode/normalizer2.h:239-242 `virtual UnicodeString & normalize(src,dest,errorCode) const = 0;` (exists)
> unicode/normalizer2.h:104-105 `getNFCInstance`; 118-119 `getNFDInstance`; 132-133 `getNFKCInstance`; 146-147 `getNFKDInstance`; 163-164 `getNFKCCasefoldInstance`; 180-181 `getNFKCSimpleCasefoldInstance`; 204-208 `getInstance(packageName,name,mode,errorCode)` (all exist)
> unicode/normalizer2.h:284-287 `normalizeSecondAndAppend`; 302-305 `append`; 320-321 `getDecomposition` (pure virtual); 393-394 `isNormalized`; 433-434 `quickCheck`; (spanQuickCheckYes seen at norm2allmodes.h:167-180, declared pure-virtual char16_t* overload at norm2allmodes.h:180 and string overload at normalizer2.h — all exist)
> icu4c/source/common/normalizer2.cpp:47-81, 256-320 and icu4c/source/common/loadednormalizer2impl.cpp:204-423 — concrete factory implementations exist.
> icu4c/source/common/norm2allmodes.h:32-406 — `Normalizer2WithImpl`, `DecomposeNormalizer2`, `ComposeNormalizer2`, `FCDNormalizer2` all exist and implement the dispatch to `Normalizer2Impl::decompose/compose/decomposeAndAppend/composeAndAppend`.
> `Normalizer2Impl::decompose`, `composePair`, `compose` referenced at norm2allmodes.h:198 (`impl.decompose(...)`), :141 (`impl.composePair(a,b)`), and compose class body (ComposeNormalizer2, norm2allmodes.h:250 onward) — functions exist in normalizer2impl.h/.cpp (declarations confirmed via includes `#include "normalizer2impl.h"` at norm2allmodes.h:26 and normalizer2.cpp:31).

### P2.7 Co-call Constraints
**Claim:** The only "paired" constraint is factory-then-use: `getXXXInstance()`/`getInstance()` must be called (and succeed) before `normalize()`/`isNormalized()`/`quickCheck()`/`spanQuickCheckYes()`/`normalizeSecondAndAppend()`/`append()`/`getDecomposition()` are invoked on the returned pointer — there is no separate "close"/"end" call. `normalizeSecondAndAppend`/`append` additionally require `first` and `second` to be different `UnicodeString` objects (checked, else `U_ILLEGAL_ARGUMENT_ERROR`), mirroring the `src`/`dest` distinctness rule for the 3-arg `normalize`.
**Evidence:**
> norm2allmodes.h:85-89 `uprv_checkCanGetBuffer(first,errorCode); ... const char16_t *secondArray=second.getBuffer(); if(&first==&second || secondArray==nullptr) { errorCode=U_ILLEGAL_ARGUMENT_ERROR; return first; }`
> norm2allmodes.h:46-50 (src/dest distinctness for normalize, 3-arg form; not reachable from the 2-arg target wrapper since dest is always the function-local).
> normalizer2.h:274 "The first and second strings must be different objects." (doc for normalizeSecondAndAppend); normalizer2.h:229 "The source and destination strings must be different objects." (doc for 3-arg normalize).

### P2.8 Prerequisite State
**Claim:** No connection/context object is required beyond ICU's own internal data (nrm2 binary data, loaded transparently by `getInstance`/`getNFxInstance` via `udata_openChoice`). For the custom-data path (`getInstance(packageName, name, mode, errorCode)`), the named `.nrm` data package must be discoverable (loadable by `udata_openChoice(packageName, "nrm", name, isAcceptable, ...)`); if it cannot be found/validated, `errorCode` is set to a failure (e.g. `U_MISSING_RESOURCE_ERROR` internally from `udata_openChoice`, or `U_INVALID_FORMAT_ERROR` if the index header is malformed) and `getInstance` returns `nullptr` — `normalize` must not be called in that case.
**Evidence:**
> icu4c/source/common/loadednormalizer2impl.cpp:76-91: `load()` calls `memory=udata_openChoice(packageName, "nrm", name, isAcceptable, this, &errorCode); if(U_FAILURE(errorCode)) return;` then validates `indexesLength<=IX_MIN_LCCC_CP` → `errorCode=U_INVALID_FORMAT_ERROR;`.
> loadednormalizer2impl.cpp:54-74 `isAcceptable()` validates the `"Nrm2"` data-format magic bytes and `formatVersion[0]==5` before accepting the data.
> loadednormalizer2impl.cpp:349-363: only when `allModes!=nullptr && U_SUCCESS(errorCode)` is an instance pointer returned; otherwise `nullptr`.

### P2.9 Result Usage
**Claim:** The `UnicodeString` result of `normalize()` is consumed by: (a) direct string comparison/assertion in tests; (b) further UnicodeString API (e.g. `.isEmpty()`, `.toUTF8()`, comparisons); (c) fed back into `isNormalized`/`quickCheck`/`getDecomposition` to cross-check normalization correctness; (d) in `normalizeUTF8`, the base-class implementation calls `normalize(src16, errorCode).toUTF8(sink)` directly chaining the result into a ByteSink.
**Evidence:**
> icu4c/source/common/normalizer2.cpp:49-61 `normalizeUTF8`: `UnicodeString src16 = UnicodeString::fromUTF8(src); normalize(src16, errorCode).toUTF8(sink);` — base-class default uses the *virtual* 3-arg normalize but shows the chaining pattern (`normalize(...).toUTF8(sink)`), i.e. result consumed by `UnicodeString::toUTF8`.
> icu4c/source/i18n/messageformat2_function_registry.cpp:169-173: `UnicodeString normalized = nfcNormalizer->normalize(s, status); if (U_FAILURE(status)) { return {}; } return normalized;` — result returned up the call stack as the function's own return value, used later as formatted text.
> icu4c/source/test/intltest/tstnorm.cpp:1755-1762: `result = n2->normalize(sh, errorCode); assertTrue(... result.isEmpty()); ... result = n2->normalize(s, errorCode); assertEquals(..., u"a\u0323\u0308", result);` — result directly compared against an expected `UnicodeString`.
> icu4c/source/test/intltest/tstnorm.cpp:1786 `assertTrue("getDecomposition(soft hyphen)", n2.getDecomposition(0xad, mapping));` — independent API used alongside normalize() results to validate per-codepoint behaviour (not literally consuming normalize's return value, but used in the same test flow to validate the Normalizer2 instance's behaviour).
> normalizer2.h:65-66 doc: "the remainder can be processed with normalizeSecondAndAppend()" — documents that `spanQuickCheckYes` index + `normalizeSecondAndAppend` on the un-normalized remainder is the intended composition pattern (result of one is input to the other) though this uses `spanQuickCheckYes`'s index rather than `normalize`'s own return.

### P2.10 Behaviour Controls
**Claim:** The processing path is selected entirely by *which Normalizer2 singleton* is used (chosen at `getInstance`/`getNFxInstance` time), not by any flag on `normalize()` itself (it takes only `src`,`errorCode`). Behaviour-selecting parameters are:
- `name` string in `getInstance(packageName,name,mode,errorCode)`: `"nfc"`, `"nfkc"`, `"nfkc_cf"`, `"nfkc_scf"`, or a custom data file name — selects which `Normalizer2Impl` (trie/data) backs the instance.
- `mode` (`UNormalization2Mode`): `UNORM2_COMPOSE`, `UNORM2_DECOMPOSE`, `UNORM2_FCD`, `UNORM2_COMPOSE_CONTIGUOUS` — selects `ComposeNormalizer2`/`DecomposeNormalizer2`/`FCDNormalizer2` and, for compose, the `onlyContiguous` flag (FCC vs NFC).
- `packageName`: `nullptr` for built-in ICU data vs. an application package name for custom `.nrm` data loaded via `udata_openChoice`.
**Evidence:**
> loadednormalizer2impl.cpp:294-309: dispatch on `name` string (`"nfc"`,`"nfkc"`,`"nfkc_cf"`,`"nfkc_scf"`) to select `Norm2AllModes::getNFCInstance/getNFKCInstance/getNFKC_CFInstance/getNFKC_SCFInstance`.
> loadednormalizer2impl.cpp:349-361: `switch(mode) { case UNORM2_COMPOSE: return &allModes->comp; case UNORM2_DECOMPOSE: return &allModes->decomp; case UNORM2_FCD: return &allModes->fcd; case UNORM2_COMPOSE_CONTIGUOUS: return &allModes->fcc; }`.
> norm2allmodes.h:252-253: `ComposeNormalizer2(const Normalizer2Impl &ni, UBool fcc) : Normalizer2WithImpl(ni), onlyContiguous(fcc) {}` — the FCC vs NFC distinction is a boolean constructor flag baked into the singleton, not a per-call parameter.
> norm2allmodes.h:195-199 vs (Compose class body, same file ~257+): `DecomposeNormalizer2::normalize` calls `impl.decompose(...)`; `ComposeNormalizer2::normalize` calls `impl.compose(..., onlyContiguous, ...)` — confirms the mode selects a structurally different core routine.
> `normalize()` itself (normalizer2.h:220-225, 239-242) has no mode/flags parameter — all branching happens via virtual dispatch on `this`.

### P2.11 Input Expectations
**Claim:** `src` is expected to be a valid (non-"open"/non-null-buffer) `UnicodeString`, i.e. any sequence of UTF-16 code units including unpaired surrogates, unassigned code points, zero length, or very large length — the function does not require pre-validated UTF-16 or NUL-termination (UnicodeString manages its own length/termination); it does require `src.getBuffer()` to be non-null, and (for the 3-arg form) `&dest != &src`.
**Evidence:**
> norm2allmodes.h:46-50: `const char16_t *sArray=src.getBuffer(); if(&dest==&src || sArray==nullptr) { errorCode=U_ILLEGAL_ARGUMENT_ERROR; ... }` — only check performed on `src` before processing; no length/content validation beyond this.
> icu4c/source/test/intltest/tstnorm.cpp:1794 `TestNormalizeIllFormedText()` — dedicated test exercising ill-formed text (lone surrogates etc.) through `Normalizer2::normalize`, confirming malformed UTF-16 is an expected/tested input (not rejected up front).
> normalizer2.h:211-218 doc only documents `errorCode` input/output contract; no format/size/termination requirement is documented for `src` itself, consistent with `UnicodeString` being ICU's general-purpose internal string representation (length-prefixed, not C-string-terminated).
> `buffer.init(src.length(), errorCode)` (norm2allmodes.h:54) sizes the `ReorderingBuffer` to `src.length()`, so very large `src.length()` stresses buffer growth/allocation inside `ReorderingBuffer::init`/append paths (per the feature description); no explicit upper bound is checked in `normalize()` itself — allocation failures surface via `errorCode` becoming `U_MEMORY_ALLOCATION_ERROR` from the buffer layer.

## Other Logic-Group Entries: call protocol and combination with the target

All entries below are declared in `unicode/normalizer2.h` and exist in the codebase (verified by direct reads above). They all combine around the same pattern: obtain a `const Normalizer2*` singleton once, then call any subset of the entry methods on it in a fuzz-driven switch/order. None of them require teardown.

### icu::Normalizer2::getInstance(packageName, name, mode, errorCode)
- **Needs before it:** nothing but a zero'd `UErrorCode`. `name` must be non-null/non-empty (else `U_ILLEGAL_ARGUMENT_ERROR`, loadednormalizer2impl.cpp:294-297).
- **Parameters/flags selecting paths:** `packageName` (nullptr ⇒ built-in data; non-null ⇒ custom `.nrm` package loaded via `udata_openChoice`); `name` (`"nfc"`,`"nfkc"`,`"nfkc_cf"`,`"nfkc_scf"`, or custom); `mode` (`UNORM2_COMPOSE`/`UNORM2_DECOMPOSE`/`UNORM2_FCD`/`UNORM2_COMPOSE_CONTIGUOUS`). See loadednormalizer2impl.cpp:286-364 (full dispatch).
- **Return value:** `const Normalizer2*` or `nullptr` on failure; caller must check `U_SUCCESS(errorCode) && result != nullptr` before use (loadednormalizer2impl.cpp:291-296, 310, 349-363).
- **Cleanup:** none — singleton owned by ICU's internal cache, cleaned up at `ucln_common` library-cleanup time (loadednormalizer2impl.cpp:318 `ucln_common_registerCleanup(...)`), not by the caller.
- **Combination with target:** this is the mandatory predecessor call; its result feeds directly as the `this` receiver of `normalize()`.

### icu::Normalizer2::getNFCInstance / getNFDInstance / getNFKCInstance / getNFKDInstance / getNFKCCasefoldInstance
- **Needs before it:** nothing but a zero'd `UErrorCode`.
- **Parameters:** only `UErrorCode&`; no mode selection (each is a fixed-mode convenience wrapper equivalent to a specific `getInstance(nullptr, name, mode, errorCode)` call — normalizer2.h:95,109,123,137,154).
- **Return value:** `const Normalizer2*`/`nullptr`, same contract as `getInstance`.
- **Cleanup:** none (singleton, see above).
- **Combination with target:** interchangeable with `getInstance` as the source of the receiver object for `normalize()`; a harness picks one of these 5 fixed-mode factories or the generic `getInstance` with a fuzzed `(packageName,name,mode)` tuple, then calls `normalize(src,errorCode)` on whichever singleton is returned.

### icu::Normalizer2::normalize(src, dest, errorCode) [virtual 3-arg form]
- **Needs before it:** a valid instance (as above); `src` and `dest` must be different `UnicodeString` objects (norm2allmodes.h:47-50 `U_ILLEGAL_ARGUMENT_ERROR` if same object or `src.getBuffer()==nullptr`).
- **Return value:** reference to `dest` (same object passed in), replaced in place; check `errorCode` same as the 2-arg target.
- **Combination:** the 2-arg target literally forwards to this (normalizer2.h:223 `normalize(src, result, errorCode);`); a harness exercising the target only needs the 2-arg overload, but may additionally call the 3-arg overload directly with a caller-owned `dest` to test the explicit-buffer path and the `&dest==&src` guard.

### icu::Normalizer2::normalizeSecondAndAppend(first, second, errorCode) / append(first, second, errorCode)
- **Needs before it:** `first` should already be normalized (not enforced, just documented — normalizer2.h:275 "should be normalized"); `first` and `second` must be different objects (norm2allmodes.h:86-89, `U_ILLEGAL_ARGUMENT_ERROR` otherwise); `first` must support `getBuffer()` write access (`uprv_checkCanGetBuffer`, norm2allmodes.h:81).
- **Parameters/flags:** `normalizeSecondAndAppend` normalizes `second` before appending (`doNormalize=true`); `append` does not re-normalize `second` (`doNormalize=false`) — both route through the same internal `normalizeSecondAndAppend(first,second,doNormalize,errorCode)` helper (norm2allmodes.h:65-74,76-104).
- **Return value:** reference to `first` (mutated in place); check `errorCode`.
- **Cleanup:** none beyond normal `UnicodeString` destruction.
- **Combination with target:** typical pattern (documented at normalizer2.h:64-66, 446-449) is: normalize/quick-check a first chunk, then use `spanQuickCheckYes` to find a safe boundary, keep the "yes" prefix, and call `normalizeSecondAndAppend` on the remainder — a harness can call `target.normalize()` on one fuzzed substring to build `first`, then call `normalizeSecondAndAppend`/`append` with a second fuzzed substring to exercise the merge/boundary logic (`Normalizer2Impl::decomposeAndAppend`/`composeAndAppend`, the "safeMiddle" logic at norm2allmodes.h:91-102).

### icu::Normalizer2::isNormalized(s, errorCode)
- **Needs before it:** valid instance; `s.getBuffer()` non-null (else `U_ILLEGAL_ARGUMENT_ERROR`, norm2allmodes.h:155-159).
- **Return value:** `UBool`; internally computed via `spanQuickCheckYes` reaching the string's end (norm2allmodes.h:160-161 `return sLimit==spanQuickCheckYes(sArray, sLimit, errorCode);`).
- **Combination with target:** commonly called before `normalize()` to skip redundant work, e.g. messageformat2_function_registry.cpp:163-169 `quickCheck(...)` then only calls `normalize()` if not already "yes" — a harness can call `isNormalized`/`quickCheck` on the same fuzzed string before/after `normalize()` to cross-check: normalizing a normalized string should be a no-op, and `isNormalized(normalize(s))` should be true.

### icu::Normalizer2::quickCheck(s, errorCode)
- **Needs before it:** same as `isNormalized`.
- **Return value:** `UNormalizationCheckResult` (`UNORM_YES`/`UNORM_NO`/`UNORM_MAYBE`); default base implementation for compose modes can return `UNORM_MAYBE`, decompose/FCD resolve directly via `isNormalized` (norm2allmodes.h:163-166 `return Normalizer2WithImpl::isNormalized(s,errorCode) ? UNORM_YES : UNORM_NO;` for the base, overridden per-mode where "maybe" applies).
- **Combination with target:** real caller `messageformat2_function_registry.cpp:163-173` — checks `quickCheck()==UNORM_YES` first, calls `normalize()` only otherwise; a harness mirrors this pattern or calls both independently on the same fuzzed string to compare results.

### icu::Normalizer2::spanQuickCheckYes(s, errorCode)
- **Needs before it:** same buffer-null check as above (norm2allmodes.h:172-176).
- **Return value:** `int32_t` index into `s` where the "yes" span ends; used (per docs at normalizer2.h:446-449) to slice `s` and feed the remainder to `normalizeSecondAndAppend`.
- **Combination with target:** `tstnorm.cpp:1758` `assertEquals("soft hyphen spanQuickCheckYes", 0, n2->spanQuickCheckYes(sh, errorCode));` alongside `normalize()` calls on the same instance/string in the same test (tstnorm.cpp:1753-1762) — a harness can call `spanQuickCheckYes` then `normalize` on the tail substring.

### icu::Normalizer2::getDecomposition(c, decomposition)
- **Needs before it:** valid instance; no `UErrorCode` parameter (pure query, cannot fail with error code — just returns `false` if `c` has no decomposition, norm2allmodes.h:110-123).
- **Return value:** `UBool`; `decomposition` `UnicodeString` is only meaningful when return is `true`.
- **Combination with target:** used independently of `normalize()` to validate per-codepoint mapping data; real test usage `tstnorm.cpp:1162-1163, 1193-1198, 1786` checks `getDecomposition` results against expectations and against `normalize()`'s output for related strings (e.g. `checkLowMappingToEmpty` at tstnorm.cpp:1784-1791 is called right after a `normalize()`-based test, tstnorm.cpp:1751/1755/1761).

### Not part of this group / not found
- No separate "close"/"destroy" function for `Normalizer2*` exists or is needed (confirmed above, P2.5).
- `normalizeUTF8` exists (normalizer2.h:266-268) but is not listed among the given Entries; it is a convenience wrapper (base impl at normalizer2.cpp:49-61 converts to UTF-16 and calls the virtual `normalize`), so if a harness also fuzzes UTF-8 input it is a legitimate additional entry, but it was not requested in the Logic Group list, so no further investigation is reported here beyond confirming its existence.

## Build notes

**Language:** C++ (fuzzer sources end in `_fuzzer.cpp`, compiled with `$CXX`).
**Evidence:**
> ossfuzz_view/icu-c4/build.sh:44-54:
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
**Include path for target's header:** `unicode/normalizer2.h` is found via `-I$SRC/icu/icu4c/source/common` (the header lives at `icu4c/source/common/unicode/normalizer2.h`), so a harness includes it as `#include "unicode/normalizer2.h"`. `-I$SRC/icu/icu4c/source/i18n` is also on the include path (needed for i18n-layer headers if the harness also touches `i18n` features, not strictly required for Normalizer2 itself which lives in `common`).
**Libraries linked:** `-licui18n -licuuc -licutu -licudata` (i18n, common/uc, tools, data) plus `$LIB_FUZZING_ENGINE`.
**Build flags:** `-DU_CHARSET_IS_UTF8=1 -DU_USING_ICU_NAMESPACE=0 -DU_ENABLE_DYLOAD=0 -DU_USE_STRTOD_L=0` (build.sh:24), static linking (`--with-data-packaging=static --enable-static --disable-shared`, build.sh:30).
**Existing fuzzer file for this feature:** `icu4c/source/test/fuzzer/normalizer2_fuzzer.cpp` exists in the tree but is currently a stub ("Gold harness replaced by an empty stub for baseline generation"); it previously (per project convention of other fuzzers in the same directory, e.g. `ucasemap_fuzzer.cpp`) would construct a `UnicodeString` from raw fuzz bytes, obtain a `Normalizer2*` singleton, and call the target plus sibling entries — confirming the target is meant to be driven directly from raw fuzzer bytes converted to a `UnicodeString` (e.g. via `UnicodeString(false, (const char16_t*)data, size/2)` or `UnicodeString::fromUTF8`), with `UErrorCode` locals initialized to `U_ZERO_ERROR` per call as shown in every real call site inspected (messageformat2_function_registry.cpp:157, tstnorm.cpp:1745-1747, nortrans.cpp:64-65).
> icu4c/source/test/fuzzer/normalizer2_fuzzer.cpp:1 "/* Gold harness replaced by an empty stub for baseline generation. */"
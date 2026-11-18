## target: icu::DateTimePatternGenerator::getBestPattern

### Overview / Definition
**Claim:** Target is declared twice (1-arg and 3-arg overload); the 1-arg overload simply forwards to the 3-arg overload with `UDATPG_MATCH_NO_OPTIONS`. Defined in `dtptngen.cpp`.
**Evidence:**
> icu4c/source/i18n/unicode/dtptngen.h:403: `U_I18N_API UnicodeString getBestPattern(const UnicodeString& skeleton, UErrorCode& status);`
> icu4c/source/i18n/unicode/dtptngen.h:424-426: `U_I18N_API UnicodeString getBestPattern(const UnicodeString& skeleton, UDateTimePatternMatchOptions options, UErrorCode& status);`
> icu4c/source/i18n/dtptngen.cpp:1153-1156:
> ```
> UnicodeString
> DateTimePatternGenerator::getBestPattern(const UnicodeString& patternForm, UErrorCode& status) {
>     return getBestPattern(patternForm, UDATPG_MATCH_NO_OPTIONS, status);
> }
> ```
> icu4c/source/i18n/dtptngen.cpp:1158-1230: full body of the options overload (mapSkeletonMetacharacters → dtMatcher->set → getBestRaw → adjustFieldTypes or getBestAppending x2 + SimpleFormatter combine).

### P2.1 Init Sequence
**Claim:** A `DateTimePatternGenerator` object must exist and be successfully constructed (via `createInstance`, `createInstance(locale)`, `createInstanceNoStdPat` (internal), or `createEmptyInstance`) *before* `getBestPattern` is called. Construction runs `initData()` (for the locale-based factories) which populates `patternMap`, `fAllowedHourFormats`, `fDefaultHourFormatChar`, `decimal`, `dateTimeFormat[]`, etc.; `getBestPattern` directly reads `fDefaultHourFormatChar`/`fAllowedHourFormats` (via `mapSkeletonMetacharacters`) and `patternMap` (via `getBestRaw`), so without a successfully-initialized generator the call either operates on an empty map (createEmptyInstance) or fails via `internalErrorCode`.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:301-334: `createInstance(status)`, `createInstance(locale,status)`, `createInstanceNoStdPat`, `createEmptyInstance` — all construct via `new DateTimePatternGenerator(...)` guarded by `LocalPointer`.
> icu4c/source/i18n/dtptngen.cpp:336-353: constructor allocates `fp`, `dtMatcher`, `distanceInfo`, `patternMap`; locale constructor calls `initData(locale, status, skipStdPatterns)`.
> icu4c/source/i18n/dtptngen.cpp:1160-1166: `getBestPattern` checks `U_FAILURE(status)` then `U_FAILURE(internalErrorCode)` (set if construction/initData failed) and bails out returning `{}` without touching the map.
> icu4c/source/i18n/dtptngen.cpp:1240-1310ish: `mapSkeletonMetacharacters` reads `fDefaultHourFormatChar`/`fAllowedHourFormats`, set up during `initData`.
> test/intltest/dtptngts.cpp:427-480: typical sequence — `createInstance(status)` check status, then `createInstance(deLocale,status)` check status, then `gen->getBestPattern(...)`.

### P2.2 Parameter Construction
**Claim:** `skeleton` is a `const UnicodeString&` — any UTF-16 string is syntactically accepted (no required null-termination check, `UnicodeString` carries its own length); there is no documented size limit, but internal fixed-size structures (e.g., `PtnSkeleton` per-field-length arrays `UDATPG_FIELD_COUNT` long, hour-run detection loops) impose practical bounds. `status` must be a `UErrorCode` set to a non-failure value on entry (`U_ZERO_ERROR` or similar) — the function immediately returns an empty string if `U_FAILURE(status)` is already true. The options overload additionally takes `UDateTimePatternMatchOptions options`, a bitmask enum (values 0.. `(1<<UDATPG_FIELD_COUNT)-1`), no range check performed — any `int32_t`-castable value is accepted and interpreted bit-by-bit in `adjustFieldTypes`.
**Evidence:**
> icu4c/source/i18n/unicode/dtptngen.h:397-398,417-419: "@param status ... which must not indicate a failure before the function call."
> icu4c/source/i18n/dtptngen.cpp:1160-1166: `if (U_FAILURE(status)) { return {}; } if (U_FAILURE(internalErrorCode)) { status = internalErrorCode; return {}; }`
> icu4c/source/i18n/unicode/udatpg.h:124-137: `UDateTimePatternMatchOptions` enum, `UDATPG_MATCH_NO_OPTIONS=0` … `UDATPG_MATCH_ALL_FIELDS_LENGTH=(1<<UDATPG_FIELD_COUNT)-1`.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (factory) → optional configure (`addPattern`/`addPatternWithSkeleton`/`setDecimal`/`setAppendItemFormat`/`setDateTimeFormat`) → use (`getBestPattern`, `replaceFieldTypes`, `getSkeleton`, `getBaseSkeleton`, repeatable many times) → destroy (`delete` the pointer, there is no separate close function; destructor frees internal `fp`, `dtMatcher`, `distanceInfo`, `patternMap`, `fAvailableFormatKeyHash`, `skipMatcher`).
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:449-456:
> ```
> DateTimePatternGenerator::~DateTimePatternGenerator() {
>     delete fAvailableFormatKeyHash;
>     delete fp;
>     delete dtMatcher;
>     delete distanceInfo;
>     delete patternMap;
>     delete skipMatcher;
> }
> ```
> test/intltest/dtptngts.cpp:427-434: `createInstance(status)` ... `delete instFromDefaultLocale;` after use.
> test/intltest/dtptngts.cpp:475-511: create → `getBestPattern` multiple times → `addPattern` → `getBestPattern` again (object is reused across many calls).
> unicode/dtptngen.h:101: "Destructor." (virtual ~DateTimePatternGenerator()), no other required paired-close call.

### P2.4 Return Value Handling
**Claim:** Return is by value (`UnicodeString`), not a pointer that needs freeing (it is a stack value with its own internal buffer management). On failure `status` is set to a non-U_SUCCESS code and the returned `UnicodeString` is the default-constructed `{}` (empty). Callers must check `U_FAILURE(status)` after the call before using the returned pattern (it may also legitimately be an empty string on genuine "no result" paths such as both date/time masks empty). No special sentinel value other than `status`.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:1160-1166,1177-1179,1185-1187,1197-1199: every early-exit path does `return {};` paired with a `status` already set to failure.
> icu4c/source/i18n/dtptngen.cpp:1200-1203: `if (datePattern.length()==0) { if (timePattern.length()==0) { resultPattern.remove(); } ... }` — legitimate empty-string return without error.
> test/intltest/dtptngts.cpp:480-486: `UnicodeString findPattern = gen->getBestPattern(...,status); SimpleDateFormat *format = new SimpleDateFormat(findPattern, deLocale, status); if (U_FAILURE(status)) { ...; delete gen; return; }` — status checked after use of result.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths, only the `DateTimePatternGenerator*` itself needs explicit `delete` (its destructor transitively frees `fp`, `dtMatcher`, `distanceInfo`, `patternMap`, `fAvailableFormatKeyHash`, `skipMatcher`). The `UnicodeString` returned by `getBestPattern` is a value type with automatic storage management (no manual free). Any objects built downstream from the returned pattern (e.g. `SimpleDateFormat*`) must be separately `delete`d by the caller.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:449-456 (destructor, shown above).
> test/intltest/dtptngts.cpp:483-492: on `SimpleDateFormat` construction failure, caller does `delete gen;` before returning — explicit single-point cleanup of the generator even on an error branch.
> test/intltest/dtptngts.cpp:433 / 467-468: `delete instFromDefaultLocale;` / `delete instFromLocale; delete cloneDTPatternGen;` after successful use.

### P2.6 API Existence
**Claim:** All functions named in this report exist in the current codebase at the cited locations (verified by direct `Read`/`Grep`).
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:302,307,317,327 (createInstance x3, createEmptyInstance)
> icu4c/source/i18n/dtptngen.cpp:1154,1159 (getBestPattern x2)
> icu4c/source/i18n/dtptngen.cpp:1313,1320 (replaceFieldTypes x2)
> icu4c/source/i18n/dtptngen.cpp:750,770 (getSkeleton, getBaseSkeleton)
> icu4c/source/i18n/dtptngen.cpp:1337 (setDecimal)
> icu4c/source/i18n/dtptngen.cpp:1488,1503,1524 (addPattern, addPatternWithSkeleton, addPatternWithOptionalSkeleton)
> icu4c/source/i18n/dtptngen.cpp:449 (~DateTimePatternGenerator)
> icu4c/source/i18n/dtptngen.cpp:2004 (clone)

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for this class; the only strict pairing is create (factory) / destroy (`delete`). `addPatternWithSkeleton` is internal-only (guarded by `U_HIDE_INTERNAL_API`), callable but documented `@internal`. There is no required "close" distinct from `delete`. `setDecimal` has no corresponding "unset" — it is a simple setter usable any number of times before/after `getBestPattern` calls (affects only the fractional-seconds substitution branch of `adjustFieldTypes`).
**Evidence:**
> icu4c/source/i18n/unicode/dtptngen.h:222-233 (addPatternWithSkeleton wrapped in `#ifndef U_HIDE_INTERNAL_API`, marked `@internal ICU 78`).
> icu4c/source/i18n/dtptngen.cpp:1337-1341 (`setDecimal` just assigns `this->decimal`).
> icu4c/source/i18n/dtptngen.cpp:449-456 (single destructor, no separate close()).

### P2.8 Prerequisite State
**Claim:** Before `getBestPattern` can usefully match anything, the generator's `patternMap` must be populated — this happens automatically from locale resource data in `initData()` (called by the locale-taking factories) or manually via `addPattern`/`addPatternWithSkeleton` (needed if using `createEmptyInstance`, which starts with an empty `patternMap`). `fDefaultHourFormatChar` and `fAllowedHourFormats` (set in `initData`) govern how `j`/`J`/`C` are mapped; with `createEmptyInstance` these stay at their class defaults, so skeleton hour-metacharacter mapping behaves differently (uses hard-coded defaults, no locale lookup) than with a locale-constructed generator.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:492-493 `initData(locale, status, skipStdPatterns)` (not called for createEmptyInstance; its ctor calls only the base `DateTimePatternGenerator(status)`, dtptngen.cpp:327-333).
> icu4c/source/i18n/dtptngen.cpp:1239-1310 `mapSkeletonMetacharacters` reads `fDefaultHourFormatChar`, `fAllowedHourFormats[0]` — values populated inside `initData`.
> icu4c/source/i18n/unicode/dtptngen.h:90-95: "Create an empty generator, to be constructed with addPattern(...) etc." — documents the prerequisite state requirement explicitly.

### P2.9 Result Usage
**Claim:** The `UnicodeString` pattern returned by `getBestPattern` is consumed by real callers mainly as the `pattern` argument to `SimpleDateFormat`'s constructor / `applyPattern`, and by `DateIntervalFormat` internals to build date/time combining patterns; it is also fed back into the generator itself (`getSkeleton`/`getBaseSkeleton`/`replaceFieldTypes`) in some flows, and even into `getBestPattern` again (`dtptngen.cpp:1976`, used in a self-consistency test-the-pattern loop).
**Evidence:**
> icu4c/source/i18n/datefmt.cpp:113: `dtpg->getBestPattern(fSkeleton, status))` feeding `SimpleDateFormat` construction (surrounding context constructs a date formatter from the returned pattern).
> icu4c/source/i18n/datefmt.cpp:470,526: static `DateFormat::getBestPattern(locale, skeleton, status)` wraps the instance method and its result is used to build date format patterns.
> icu4c/source/i18n/dtitvfmt.cpp:854-902,952-997,1260-1319: multiple uses of `DateFormat::getBestPattern(...)` results assembled as `datePattern`/`timePattern`/`convertedPattern` strings later combined via `SimpleFormatter` to build interval-format patterns.
> icu4c/source/i18n/smpdtfmt.cpp:774: `timePattern = dtpg->getBestPattern(timeSkeleton, useStatus);` then used to rebuild a `SimpleDateFormat` pattern.
> test/intltest/dtptngts.cpp:480-511: `findPattern = gen->getBestPattern(...)` → `new SimpleDateFormat(findPattern, deLocale, status)` → `format->setTimeZone(...)` → `format->format(sampleDate, ...)`.
> icu4c/source/i18n/dtptngen.cpp:1976: `UnicodeString trial = getBestPattern(current.getPattern(), status);` (internal self-check loop in `DTRedundantEnumeration`/related enumeration code, re-querying getBestPattern on a stored pattern's text).

### P2.10 Behaviour Controls
**Claim:** Key behavior-selecting inputs: (1) the skeleton text itself, specifically occurrences of `j`/`J`/`C` hour metacharacters and their repeat-length (1-6), interpreted by `mapSkeletonMetacharacters` to select `hourChar`/`dayPeriodChar`/flags (`kDTPGFixFractionalSeconds`-style flags, `kDTPGSkeletonUsesCapJ`, etc. — see `flags` int32_t threaded through); (2) the `UDateTimePatternMatchOptions options` bitmask (`UDATPG_MATCH_NO_OPTIONS`/`UDATPG_MATCH_HOUR_FIELD_LENGTH`/`UDATPG_MATCH_MINUTE_FIELD_LENGTH`/`UDATPG_MATCH_SECOND_FIELD_LENGTH`/`UDATPG_MATCH_ALL_FIELDS_LENGTH`), consumed only inside `adjustFieldTypes` (passed through from `getBestPattern`/`getBestAppending`/`replaceFieldTypes`) to decide whether field widths are forced from the skeleton into the chosen pattern; (3) whether the matched candidate covers all requested fields (`distanceInfo->missingFieldMask==0 && extraFieldMask==0`) — selects the single-`adjustFieldTypes` fast path vs. the dual `getBestAppending(date)+getBestAppending(time)`+`SimpleFormatter` combine path; (4) `monthFieldLen`/weekday presence of the matched skeleton selects `UDAT_FULL/LONG/MEDIUM/SHORT` style used to fetch the date-time combining format via `getDateTimeFormat(style,status)`.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:1172-1173: `dateMask`/`timeMask` bit-split of `UDATPG_FIELD_COUNT`.
> icu4c/source/i18n/dtptngen.cpp:1176: `UnicodeString patternFormMapped = mapSkeletonMetacharacters(patternForm, &flags, status);`
> icu4c/source/i18n/dtptngen.cpp:1189-1193: fast path when no missing/extra fields: `resultPattern = adjustFieldTypes(*bestPattern, specifiedSkeleton, flags, options); return resultPattern;`
> icu4c/source/i18n/dtptngen.cpp:1194-1229: fallback path: `getBestAppending(neededFields & dateMask, flags, status, options)` + `getBestAppending(neededFields & timeMask, ...)`, style selection via `monthFieldLen`/weekday length, then `SimpleFormatter(dtFormat,2,2,status).format(timePattern, datePattern, resultPattern, status)`.
> icu4c/source/i18n/unicode/udatpg.h:124-137: options enum values.
> icu4c/source/i18n/dtptngen.cpp:1256-1272 (mapSkeletonMetacharacters body): branch on `patChr == LOW_J || patChr == CAP_C`, computing `hourLen`/`dayPeriodLen` from repeated-char count, selecting `fDefaultHourFormatChar` vs. locale `fAllowedHourFormats`.

### P2.11 Input Expectations
**Claim:** The skeleton is expected to be a short sequence of date/time pattern-letter runs (each run is a repeated single field-letter, e.g. "MMM", "dd") optionally containing single-quote-delimited literal text (quote handling toggled by `inQuoted` flag, scanning char-by-char); no explicit maximum length is enforced in `getBestPattern`/`mapSkeletonMetacharacters` itself (loop is `for (patPos = 0; patPos < patLen; patPos++)` bounded by `patternForm.length()`), no required NUL-termination because `UnicodeString` tracks length explicitly. Internally, field lengths get stored into fixed-size per-field arrays inside `PtnSkeleton`/`DateTimeMatcher` (bounded by `UDATPG_FIELD_COUNT`), so arbitrarily long runs of the same letter are only used to derive `hourLen`/`dayPeriodLen`/field-length counts, not to directly index without bound-checks (length typically clamped via `getFieldLength`/matcher set logic, not inspected further here). There is no "magic header" — any UTF-16 string is parsed by `mapSkeletonMetacharacters` then by `dtMatcher->set(...)`/`FormatParser` for field tokenization.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:1244-1246: `UBool inQuoted = false; int32_t patPos, patLen = patternForm.length(); for (patPos = 0; patPos < patLen; patPos++) { char16_t patChr = patternForm.charAt(patPos); if (patChr == SINGLE_QUOTE) { inQuoted = !inQuoted; } ...`
> icu4c/source/i18n/unicode/dtptngen.h:394-396: "@param skeleton The skeleton is a pattern containing only the variable fields. For example, 'MMMdd' and 'mmhh' are skeletons."
> icu4c/source/i18n/dtptngen.cpp:1256-1261: repeated-char counting loop (`while (patPos+1 < patLen && patternForm.charAt(patPos+1)==patChr) { extraLen++; patPos++; }`) — no explicit cap on repeat count other than string length.

## Other Logic-Group Entries: call protocol and combination with the target

### icu::DateTimePatternGenerator::createInstance(UErrorCode&) / createInstance(const Locale&, UErrorCode&)
**Claim:** Static factory, no prerequisite. `createInstance(status)` uses `Locale::getDefault()`; `createInstance(locale,status)` runs `initData(locale,status,false)` (standard patterns loaded). Returns `DateTimePatternGenerator*` or `nullptr` on failure (check `U_FAILURE(status)`); caller owns the pointer and must `delete` it. Must be called (exactly once per instance) before any `getBestPattern`/`addPattern`/`replaceFieldTypes`/`getSkeleton`/`getBaseSkeleton`/`setDecimal` call on that instance, since those are non-static instance methods.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:301-314: `createInstance(status)` delegates to `createInstance(Locale::getDefault(), status)`; `createInstance(locale,status)` does `LocalPointer<DateTimePatternGenerator> result(new DateTimePatternGenerator(locale, status), status); return U_SUCCESS(status) ? result.orphan() : nullptr;`
> icu4c/source/i18n/dtptngen.cpp:349-353: locale ctor calls `initData(locale, status, skipStdPatterns)`.
> test/intltest/dtptngts.cpp:427-434,439-443,475-479: typical pattern — check `status` immediately after `createInstance`, `dataerrln`+`return` on failure, else continue to use then `delete`.
**Combination with target:** Must precede the target call on the same object; errors from `createInstance` (status failure) mean the object is `nullptr` and must not be used.

### icu::DateTimePatternGenerator::createEmptyInstance(UErrorCode&)
**Claim:** Alternative factory producing a generator with an *empty* `patternMap` (no `initData` call, no locale resource loading). Intended to be followed by `addPattern`/`addPatternWithSkeleton` calls to populate the map before `getBestPattern` is useful. Still must be `delete`d by caller.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:327-333: `createEmptyInstance` builds `new DateTimePatternGenerator(status)` (the base ctor only, no `initData`).
> icu4c/source/i18n/unicode/dtptngen.h:89-95: "Create an empty generator, to be constructed with addPattern(...) etc."
**Combination with target:** If the harness uses `createEmptyInstance`, it should call `addPattern`/`addPatternWithSkeleton` one or more times before `getBestPattern`, otherwise `getBestRaw` has nothing to match and `getBestPattern` likely returns an incomplete/empty pattern (missing/extra field mask nonzero) exercising the `getBestAppending` fallback path with empty results.

### icu::DateTimePatternGenerator::addPattern(const UnicodeString& pattern, UBool override, UnicodeString& conflictingPattern, UErrorCode& status)
**Claim:** Instance method; requires a constructed generator (`internalErrorCode` checked first). Parses `pattern` into a `skeleton`/`basePattern` via `DateTimeMatcher::set` + `getBasePattern`, and inserts into `patternMap` via `addPatternWithOptionalSkeleton` (skeletonToUse=nullptr). `override` (UBool, true/false) controls whether a pre-existing base-pattern entry gets replaced. `conflictingPattern` is an output `UnicodeString&` filled with the previous pattern when a conflict is detected. Returns `UDateTimePatternConflict` (`UDATPG_NO_CONFLICT`, `UDATPG_BASE_CONFLICT`, `UDATPG_CONFLICT`) — callers should inspect this return and `status` together. No explicit cleanup beyond the owning generator's destructor (patternMap entries freed when generator is deleted).
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:1487-1500: `addPattern` body, delegates to `addPatternWithOptionalSkeleton(pattern, nullptr, override, conflictingPattern, status)`.
> icu4c/source/i18n/unicode/dtptngen.h:197-220: doc — "If the pattern has the same skeleton as an existing pattern, and the override parameter is set, then the previous value is overridden... conflicting status is set and previous value is stored in conflictingPattern."
> test/intltest/dtptngts.cpp:504-506: `conflictingStatus = gen->addPattern(UnicodeString("d'. von' MMMM", -1, US_INV), true, conflictingPattern, status); if (U_FAILURE(status)) { errln(...); }`
**Combination with target:** Called on an already-created generator, before `getBestPattern`, to inject attacker-controlled candidate patterns into `patternMap`, directly affecting which pattern `getBestRaw`/`getBestPattern` returns for subsequent skeletons.

### icu::DateTimePatternGenerator::addPatternWithSkeleton(const UnicodeString& pattern, const UnicodeString& skeletonToUse, UBool override, UnicodeString& conflictingPattern, UErrorCode& status)  [internal API]
**Claim:** Marked `@internal ICU 78`, guarded by `#ifndef U_HIDE_INTERNAL_API` in the header — it is compiled into the library and callable from a fuzz harness that includes the internal declaration, but is not part of the `@stable` public surface. Delegates to the same `addPatternWithOptionalSkeleton` as `addPattern`, but passes an explicit `skeletonToUse` (simulating `availableFormats` locale-data entries) instead of deriving the skeleton from `pattern`. Same return type/semantics as `addPattern`.
**Evidence:**
> icu4c/source/i18n/unicode/dtptngen.h:222-233 (declaration, inside `#ifndef U_HIDE_INTERNAL_API ... #endif`), marked `@internal ICU 78`.
> icu4c/source/i18n/dtptngen.cpp:1502-1511: `addPatternWithSkeleton` → `addPatternWithOptionalSkeleton(pattern, &skeletonToUse, override, conflictingPattern, status)`.
> icu4c/source/i18n/dtptngen.cpp:1513-1522 comment: explains conflict-resolution differences when a skeleton is explicitly specified (simulating availableFormats).
**Combination with target:** Same as `addPattern` but gives the harness finer control over the skeleton associated with an injected pattern, exercising more of `addPatternWithOptionalSkeleton`'s conflict logic before `getBestPattern` searches the map. Caution: it is an internal API — P3 (public-API boundary) should be weighed by the harness generator; it does exist and is linkable, confirmed above.

### icu::DateTimePatternGenerator::replaceFieldTypes(const UnicodeString& pattern, const UnicodeString& skeleton, UErrorCode& status) and the 4-arg overload with `UDateTimePatternMatchOptions options`
**Claim:** Instance method; requires constructed generator (`internalErrorCode` check). Does NOT use `patternMap`/`getBestRaw` at all — it calls `dtMatcher->set(skeleton, fp)` directly (mutating the *same* `dtMatcher` member also used internally by `getBestPattern`!) then `adjustFieldTypes(pattern, nullptr, kDTPGNoFlags, options)`. Because it mutates `dtMatcher` (an owned pointer member, not a local), calling `replaceFieldTypes` on a generator and then calling `getBestPattern` on the *same* generator is safe (getBestPattern always re-`set`s `dtMatcher` itself), but callers should be aware the two share state for the duration of a single call. Returns adjusted pattern `UnicodeString`; no special cleanup.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:1312-1317 (1-arg-forward), 1319-1334 (impl): `dtMatcher->set(skeleton, fp); UnicodeString result = adjustFieldTypes(pattern, nullptr, kDTPGNoFlags, options); return result;`
> icu4c/source/i18n/unicode/dtptngen.h:428-446: doc, "Adjusts the field types (width and subtype) of a pattern to match what is in a skeleton... This is used internally to get the best match for the input skeleton, but can also be used externally."
> test/intltest/dtptngts.cpp:535: `UnicodeString newPattern = gen->replaceFieldTypes(pattern, UnicodeString("vvvv"), status);`
**Combination with target:** Independent consumer entry that exercises the same `adjustFieldTypes` core as the target without going through `getBestRaw`; can be called before or after `getBestPattern` on the same generator instance (no additional init needed beyond the shared generator construction).

### icu::DateTimePatternGenerator::getSkeleton(const UnicodeString& pattern, UErrorCode& /*status*/) and getBaseSkeleton(...)
**Claim:** Instance methods that do NOT touch generator state (`patternMap`, `dtMatcher` member, `fAllowedHourFormats`, etc.) — each allocates a *local* `FormatParser fp2`, local `DateTimeMatcher matcher`, local `PtnSkeleton localSkeleton`, calls `matcher.set(pattern, &fp2, localSkeleton)`, and returns `localSkeleton.getSkeleton()` / `getBaseSkeleton()`. `status` parameter is actually unused (commented `/*status*/`) in both. So these do not require generator state beyond having a valid `this` pointer (object must exist, trivially satisfied once constructed) but do NOT depend on `initData`/`addPattern` having been run. Pure utility methods (their `static` siblings `staticGetSkeleton`/`staticGetBaseSkeleton` are the "canonical" documented replacements).
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:749-757: `getSkeleton` body shown above, local objects only.
> icu4c/source/i18n/dtptngen.cpp:769-776: `getBaseSkeleton` body, same pattern.
> icu4c/source/i18n/unicode/dtptngen.h:152-157,190-195: header comments note these are legacy wrappers; "Use staticGetSkeleton()/staticGetBaseSkeleton() instead."
> test/intltest/dtptngts.cpp:577,582: `UnicodeString retSkeleton = gen->getSkeleton(pattern, status); ... retSkeleton = gen->getBaseSkeleton(pattern, status);`
**Combination with target:** Can be called on an arbitrary attacker-controlled `pattern` string (independent of skeleton input to the target) at any point once the generator object exists; results (skeleton strings) could in turn be fed back as the `skeleton` argument to `getBestPattern`/`replaceFieldTypes` to chain fuzzed inputs.

### icu::DateTimePatternGenerator::setDecimal(const UnicodeString& decimal)
**Claim:** Simple setter, no return value, no prerequisite beyond object existence; stores `decimal` member (NUL-terminated for C API via `getTerminatedBuffer()`), later read by `adjustFieldTypes` when substituting fractional-second patterns (e.g. skeleton contains `S` fields). Default value for locale-constructed generators comes from `setDecimalSymbols` (called inside `initData`) using `DecimalFormatSymbols` for the locale; for `createEmptyInstance` the default is whatever `UnicodeString` default-constructs to (empty), unless `setDecimal` is called explicitly.
**Evidence:**
> icu4c/source/i18n/dtptngen.cpp:1336-1341: `setDecimal` body.
> icu4c/source/i18n/dtptngen.cpp:1478-1485 `setDecimalSymbols`: `decimal = dfs.getSymbol(DecimalFormatSymbols::kDecimalSeparatorSymbol); decimal.getTerminatedBuffer();` — called during `initData`.
> test/intltest/dtptngts.cpp:453,546: `instFromLocale->setDecimal(newDecimalSymbol);` and `gen->setDecimal(newDecimal); ... getDecimal()` round-trip check.
**Combination with target:** Call before `getBestPattern` (or before `replaceFieldTypes`) with a skeleton containing fractional-second fields (`S`) to drive the fractional-seconds substitution branch inside `adjustFieldTypes` with attacker-controlled decimal-separator content.

## Build notes

**Claim:** Fuzzers are compiled as C++ TUs against ICU's own built static libraries (`libicui18n`, `libicuuc`, `libicutu`, `libicudata`), with include paths to `icu4c/source/common` and `icu4c/source/i18n` (the latter contains `unicode/dtptngen.h`, `unicode/udatpg.h`). `ICU` is built first via `runConfigureICU` with `--enable-static --disable-shared`, then each `*_fuzzer.cpp` under `icu4c/source/test/fuzzer/` is compiled+linked individually against `locale_util.o` + the static libs. `LIB_FUZZING_ENGINE` supplies the libFuzzer `main`/driver. There is already a stub `date_time_pattern_generator_fuzzer.cpp` present for this exact target (currently gutted to a no-op baseline that just keeps the two `getBestPattern` overloads linked via `__asm__` symbol references), which the downstream Harness Generator is expected to replace with real logic.
**Evidence:**
> ossfuzz_view/icu-c1/build.sh:28-30: `/bin/bash $SRC/icu/icu4c/source/runConfigureICU Linux --with-library-bits=64 --with-data-packaging=static --enable-static --disable-shared`
> ossfuzz_view/icu-c1/build.sh:44-54:
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
> ossfuzz_view/icu-c1/build.sh:24: `DEFINES="-DU_CHARSET_IS_UTF8=1 -DU_USING_ICU_NAMESPACE=0 -DU_ENABLE_DYLOAD=0 -DU_USE_STRTOD_L=0"` applied to both CFLAGS/CXXFLAGS.
> icu4c/source/test/fuzzer/date_time_pattern_generator_fuzzer.cpp:1-17: current stub file for this exact target (to be replaced), confirms the fuzzer file is named `date_time_pattern_generator_fuzzer.cpp` and is C++ (`#ifdef __cplusplus` / `extern "C"` wrapper), and that the mangled symbols for both `getBestPattern` overloads resolve against `icu_74::DateTimePatternGenerator::getBestPattern` (namespace-versioned symbol `icu_74` observed in the `__asm__` mangled names).
> Header include path evidence: `unicode/dtptngen.h` lives under `icu4c/source/i18n/unicode/`, reachable via the `-I$SRC/icu/icu4c/source/i18n` include flag in build.sh, consistent with project convention `#include <unicode/dtptngen.h>`.
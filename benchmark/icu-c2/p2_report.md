## target: icu::ListFormatter::format

### P2.1 Init Sequence
**Claim:** A `ListFormatter` instance must be obtained from one of the static
`createInstance` factory methods before `format()` can be called. There is no
separate global library-init call required beyond normal ICU data loading
(handled internally by `ures_open` et al.). `format()` is a `const` instance
method, so it can be called multiple times on the same formatter.

**Evidence:**
> icu4c/source/i18n/unicode/listformatter.h:219-220: `UnicodeString& format(const UnicodeString items[], int32_t n_items, UnicodeString& appendTo, UErrorCode& errorCode) const;` — only declared as an instance (non-static) method.
> icu4c/source/i18n/unicode/listformatter.h:274: `ListFormatter() = delete;` — the default constructor is deleted, so the only ways to build a usable object are the public/internal constructors reached via `createInstance`.
> icu4c/source/test/intltest/listformattertest.cpp:132-138: `LocalPointer<ListFormatter> formatter(ListFormatter::createInstance(Locale(locale_string), errorCode)); ... formatter->format(...)` — createInstance always precedes format in every test.
> icu4c/source/i18n/listformatter.cpp:505-535: `createInstance(UErrorCode&)`, `createInstance(Locale,UErrorCode&)`, `createInstance(Locale,UListFormatterType,UListFormatterWidth,UErrorCode&)` all funnel to the private `createInstance(Locale, const char* style, UErrorCode&)` which calls `getListFormatInternal` then `new ListFormatter(listFormatInternal)`.

### P2.2 Parameter Construction
**Claim:** `items` is a raw C-array (`const UnicodeString items[]`) of `n_items`
valid `UnicodeString` objects (may be empty strings; `UnicodeString` owns its
own buffer so no extra allocation by caller besides array storage). `n_items`
is `int32_t`, must be `>= 0` (0, 1, 2, and >2 all have dedicated code paths —
no special "must be positive" check; 0 is legal and produces an empty list).
`appendTo` is a caller-owned `UnicodeString&` that receives appended output
(not overwritten — existing content is preserved, result appended after it).
`errorCode` must be a `UErrorCode` initialized to `U_ZERO_ERROR` (or another
non-failure value) before the call; if it already indicates failure, ICU
convention is that most APIs short-circuit, though `format()`/`formatStringsToValue`
itself does not explicitly check incoming `errorCode` state before starting
(it is set internally as needed) — see evidence below for exact checks performed.

**Evidence:**
> icu4c/source/i18n/unicode/listformatter.h:219-220: parameter types `const UnicodeString items[]`, `int32_t n_items`, `UnicodeString& appendTo`, `UErrorCode& errorCode`.
> icu4c/source/i18n/listformatter.cpp:667-727 (`formatStringsToValue`): explicit branches for `nItems == 0`, `nItems == 1`, `nItems == 2`, and `else` (>2), each calling `FormattedListBuilder` and `result.append(...)`.
> icu4c/source/test/intltest/listformattertest.cpp:829-843 (`TestInt32Overflow`): shows very large/repeated items can trigger `U_INPUT_TOO_LONG_ERROR` — i.e. there is an internal length-overflow guard (inside `FormattedStringBuilder`/`append`), not an explicit parameter-range check in `format` itself.

### P2.3 Object Lifecycle
**Claim:** Create (`ListFormatter::createInstance`) → use (`format` /
`formatStringsToValue`, any number of times, read-only/const) → destroy
(`delete` the `ListFormatter*`, or let a smart pointer such as `LocalPointer`
go out of scope). The object is immutable after construction (format is
`const`).

**Evidence:**
> icu4c/source/i18n/unicode/listformatter.h:151: "An immutable class for formatting a list" (class doc comment).
> icu4c/source/i18n/listformatter.cpp:545-547: `ListFormatter::~ListFormatter() { delete owned; }` — destructor only frees `owned` (its own internally-allocated `ListFormatInternal`); if constructed by `createInstance`, `data` points at a shared cached entry (`listPatternHash`) and is NOT owned/freed by the instance.
> icu4c/source/test/intltest/listformattertest.cpp:132 and 279/301/505/534/561/625/677/760/798/834/848: all call sites wrap the raw pointer in `LocalPointer<ListFormatter>` immediately after `createInstance`, guaranteeing destruction at scope exit.

### P2.4 Return Value Handling
**Claim:** `format()` returns `appendTo` by reference (same object passed in,
now containing the appended result) purely for chaining convenience — the
real success/failure indicator is `errorCode`. Callers must check
`U_FAILURE(errorCode)` after the call; on failure the content of `appendTo`
is unspecified/unreliable.

**Evidence:**
> icu4c/source/i18n/unicode/listformatter.h:215-216: "@param errorCode ICU error code, set if there is an error. @return Formatted string combining the elements of items, appended to appendTo."
> icu4c/source/i18n/listformatter.cpp:643-661: `format()` calls `formatStringsToValue`, then `result.appendTo(appendable, errorCode)`; it does not early-return/throw on error itself — errorCode propagates through.
> icu4c/source/test/intltest/listformattertest.cpp:166-169 (`RecordFourCases`): `if (U_FAILURE(errorCode)) { errln(...); return false; }` right after the loop of `format()` calls — standard check pattern.
> icu4c/source/test/intltest/listformattertest.cpp:829-843 (`TestInt32Overflow`): `status.expectErrorAndReset(U_INPUT_TOO_LONG_ERROR);` — shows a concrete failure code produced by the core and expected to be checked by the caller.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths, the only cleanup owed by the
caller is deleting the `ListFormatter*` returned by `createInstance` (or
letting its smart-pointer wrapper go out of scope); `UnicodeString items[]`
and `appendTo` are stack/caller-managed with automatic destruction; no
separate "close"/"free" call exists for `format()`'s result since it's
written directly into `appendTo`. If using `formatStringsToValue`, the
returned `FormattedList` is a value type whose destructor
(`FormattedList::~FormattedList`, generated by the
`UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL` macro) deletes its internal
`FormattedListData*` automatically — no manual free needed.

**Evidence:**
> icu4c/source/i18n/listformatter.cpp:545-547: destructor deletes `owned` only.
> icu4c/source/i18n/formattedval_impl.h:211-220 (`UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL`): `Name::~Name() { delete fData; fData = nullptr; }` — instantiated for `FormattedList` at icu4c/source/i18n/listformatter.cpp:253 (`UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL(FormattedList)`).
> icu4c/source/test/intltest/listformattertest.cpp:260-271 (`RunTestFieldPositionIteratorWithFormatter`): `FormattedList fl = formatter->formatStringsToValue(...)` is a stack-local value, destroyed automatically at function return — no explicit delete anywhere in the test file.

### P2.6 API Existence
**Claim:** All functions cited in this report exist in the current codebase
at the stated locations.

**Evidence:**
> `ListFormatter::format` (both overloads): icu4c/source/i18n/listformatter.cpp:634, 643 (also declared icu4c/source/i18n/unicode/listformatter.h:219, 242).
> `ListFormatter::formatStringsToValue`: icu4c/source/i18n/listformatter.cpp:663 (declared listformatter.h:233).
> `ListFormatter::createInstance` (3 overloads + private style one): icu4c/source/i18n/listformatter.cpp:505, 510, 514, 524 (declared listformatter.h:175, 186, 198, 267).
> `ListFormatter::~ListFormatter`: icu4c/source/i18n/listformatter.cpp:545.
> `FormattedList::toString/appendTo/nextPosition`: generated via macro at icu4c/source/i18n/listformatter.cpp:253, declared icu4c/source/i18n/unicode/listformatter.h:119,125,128.
> `getListFormatInternal`, `loadListFormatInternal`, `createPatternHandler`, `shouldChangeToE`, `shouldChangeToU`, `shouldChangeToVavDash`, `FormattedListBuilder::append`: icu4c/source/i18n/listformatter.cpp:311, 464, 173, 133, 150, 167, 572 respectively.

### P2.7 Co-call Constraints
**Claim:** `createInstance` / destructor (`delete`/scope-exit) is the only
strict pair for the `ListFormatter` object itself (create-before-use,
destroy-after-use). There is no lock/unlock or begin/commit pairing exposed
to the caller — internal locking (`Mutex m(&listFormatterMutex)`) is private
to the pattern-cache implementation and invisible to API users. For the
`FormattedList` consumer surface, `formatStringsToValue` (producer) pairs
with any of `toString`/`toTempString`/`appendTo`/`nextPosition` (consumers);
these can be called in any order/any number of times on the same
`FormattedList` since it's immutable, and object lifetime ends when the
`FormattedList` value is destroyed.

**Evidence:**
> icu4c/source/i18n/listformatter.cpp:320-351 (`getListFormatInternal`): `static UMutex listFormatterMutex; Mutex m(&listFormatterMutex);` — internal cache locking, not caller-visible.
> icu4c/source/test/intltest/listformattertest.cpp:260-261: `FormattedList fl = formatter->formatStringsToValue(data, n, errorCode); UnicodeString actual = fl.toString(errorCode);` then line 271 `ExpectPositions(fl, ...)` calls `nextPosition` on the same `fl` afterward — multiple consumer calls on one produced value.

### P2.8 Prerequisite State
**Claim:** Before `format`/`formatStringsToValue` can succeed, the
`ListFormatter`'s internal `data` (a `const ListFormatInternal*`) must be
non-null and fully populated with `twoPattern`/`startPattern`/`middlePattern`/
`endPattern`/`patternHandler`, which is only guaranteed if `createInstance`
returned successfully (`U_SUCCESS(errorCode)`); this in turn requires ICU
locale/resource data (CLDR `listPattern` resource bundle for the chosen
locale+style) to be loadable via `ures_open`/`ures_getByKeyWithFallback`.
If resource loading fails (e.g. missing style/locale data), `createInstance`
returns `nullptr` and the caller must not call `format` on it.

**Evidence:**
> icu4c/source/i18n/listformatter.cpp:464-503 (`loadListFormatInternal`): opens `ures_open(nullptr, locale.getName(), &errorCode)`, requires `listPattern` table and non-empty `two/start/middle/end`, else `errorCode = U_MISSING_RESOURCE_ERROR; return nullptr;`.
> icu4c/source/i18n/listformatter.cpp:524-535 (`createInstance(locale, style, errorCode)`): `if (U_FAILURE(errorCode)) { return nullptr; }` before constructing `ListFormatter`.
> icu4c/source/test/intltest/listformattertest.cpp:133-136: caller must check `U_FAILURE(errorCode)` right after `createInstance` and bail out (`dataerrln(...); return;`) before ever calling `format`.

### P2.9 Result Usage
**Claim:** `format()`'s result is consumed in place via the caller-supplied
`appendTo` `UnicodeString&` (no separate "get result" call needed — callers
typically print it, compare it, or pass it onward as a normal
`UnicodeString`). `formatStringsToValue()`'s returned `FormattedList` is
consumed by `FormattedList::toString()`/`toTempString()` (materialize full
string), `FormattedList::appendTo(Appendable&, UErrorCode&)` (stream into any
`Appendable`, which `format()` itself uses via `UnicodeStringAppendable`),
and `FormattedList::nextPosition(ConstrainedFieldPosition&, UErrorCode&)`
(iterate per-field/per-element span positions, filtered via
`cfpos.constrainCategory(UFIELD_CATEGORY_LIST)` or
`cfpos.constrainField(UFIELD_CATEGORY_LIST, ULISTFMT_ELEMENT_FIELD)`).

**Evidence:**
> icu4c/source/i18n/listformatter.cpp:643-661: `UnicodeStringAppendable appendable(appendTo); result.appendTo(appendable, errorCode);` then optionally `result.nextPosition(cfpos, errorCode)` to compute `offset` for `index >= 0` — the internal `format(...,index,offset,...)` overload used by MeasureFormat.
> icu4c/source/test/intltest/listformattertest.cpp:260-261: `FormattedList fl = ...; UnicodeString actual = fl.toString(errorCode);`
> icu4c/source/test/intltest/listformattertest.cpp:66-99 (`ExpectPositions`): `cfp.constrainCategory(UFIELD_CATEGORY_LIST); while (iter.nextPosition(cfp, status)) { ... cfp.getField()/getStart()/getLimit() ... }`
> icu4c/source/test/intltest/listformattertest.cpp:804-823 (`TestNextPosition`): loops `n=1..4`, calls `formatStringsToValue`, then `nextPosition` with `cfpos.constrainCategory(UFIELD_CATEGORY_LIST)`, counting `ULISTFMT_ELEMENT_FIELD` occurrences — demonstrates the field-iteration consumer path exercised across all item counts and all 9 type×width combinations.
> icu4c/source/test/intltest/listformattertest.cpp:846-858 (`Test21871`): uses `cfp.constrainField(UFIELD_CATEGORY_LIST, ULISTFMT_ELEMENT_FIELD)` then repeatedly calls `nextPosition` checking `getStart()/getLimit()` and the final `assertFalse` once exhausted.

### P2.10 Behaviour Controls
**Claim:** Behaviour of `format`/`formatStringsToValue` is controlled indirectly
through: (1) `n_items` (0 / 1 / 2 / >2 select different branches in
`formatStringsToValue`, each using a different subset of
two/start/middle/end patterns); (2) the **locale** passed to `createInstance`,
which selects different CLDR two/start/middle/end patterns and, for `"es"`
and `"he"`, a different `ContextualHandler` subclass built by
`createPatternHandler`; (3) `UListFormatterType` × `UListFormatterWidth`
passed to the 3-arg `createInstance`, mapped by `typeWidthToStyleString` to a
CLDR style key ("standard","standard-short","standard-narrow","or","or-short",
"or-narrow","unit","unit-short","unit-narrow") that changes which resource
bundle (hence which literal/argument splice points) is loaded; (4) the first
character / content of individual `items[]` strings, which the
`ContextualHandler::getTwoPattern`/`getEndPattern` test functions
(`shouldChangeToE`, `shouldChangeToU` for "es"; `shouldChangeToVavDash` via
`uscript_getScript` for "he") inspect to choose between two pre-built
pattern variants.

**Evidence:**
> icu4c/source/i18n/listformatter.cpp:667-698 (`formatStringsToValue`): explicit `if (nItems == 0) ... else if (nItems == 1) ... else if (nItems == 2) ... [else >2]` branches.
> icu4c/source/i18n/listformatter.cpp:355-397 (`typeWidthToStyleString`): full switch mapping `(type,width)` to style strings; returns `nullptr` for invalid combos causing `U_ILLEGAL_ARGUMENT_ERROR` (listformatter.cpp:517-519).
> icu4c/source/i18n/listformatter.cpp:173-252 (`createPatternHandler`): `if (uprv_strcmp(lang, "es") == 0) { ... shouldChangeToE ... shouldChangeToU ... } ... if (uprv_strcmp(lang, "he") == 0) { ... shouldChangeToVavDash ... }` (Hebrew branch is further down in the same function, truncated view at lines 173-252 shows the Spanish e/o branches; Hebrew handling references `shouldChangeToVavDash` defined at line 167).
> icu4c/source/i18n/listformatter.cpp:105-113 (`ContextualHandler::getTwoPattern/getEndPattern`): `return (test)(text) ? thenTwoPattern : twoPattern;` — the per-item test function selects the pattern used for that item.
> icu4c/source/test/intltest/listformattertest.cpp:785-826 (`TestNextPosition`): iterates over `locales = {"en","es","zh","ja"}`, all 3 widths, all 3 types — demonstrates the full control-surface combination used by the project's own tests.

### P2.11 Input Expectations
**Claim:** `items[]` elements are plain in-memory `UnicodeString` (UTF-16)
objects; there is no required header/magic/terminator beyond normal
`UnicodeString` semantics (empty string allowed, embedded NUL/any code point
allowed). `n_items` must accurately describe the number of valid entries in
`items[]` (no bounds-checking against a separately-stored array length is
possible — caller is trusted). There is an internal, not parameter-level,
length guard: concatenating all items plus literal pattern text into the
`FormattedStringBuilder` can overflow an internal capacity and yields
`U_INPUT_TOO_LONG_ERROR` rather than a crash. Each `SimpleFormatter` pattern
used internally (`twoPattern`, `startPattern`, `middlePattern`, `endPattern`)
is expected to have exactly 2 arguments (`{0}`,`{1}`); `FormattedListBuilder::append`
explicitly checks this and raises `U_INTERNAL_PROGRAM_ERROR` otherwise
(this is a CLDR-data invariant, not directly caller-controlled through the
`format` parameters, but matters if a caller constructs a `ListFormatter`
via the internal `ListFormatData`-based constructor with custom patterns).

**Evidence:**
> icu4c/source/i18n/listformatter.cpp:576-579: `if (pattern.getArgumentLimit() != 2) { status = U_INTERNAL_PROGRAM_ERROR; return; }` inside `FormattedListBuilder::append`.
> icu4c/source/test/intltest/listformattertest.cpp:829-843 (`TestInt32Overflow`): constructs 16 copies of a very long surrogate-pair-laden `UnicodeString` and expects `U_INPUT_TOO_LONG_ERROR`, demonstrating the internal size-overflow guard triggered purely by item content/count, not by any explicit size parameter.
> icu4c/source/i18n/listformatter.cpp:167-171 (`shouldChangeToVavDash`): `if (text.isEmpty()) { return false; } ... uscript_getScript(text.char32At(0), &status)` — reads the first code point of an item string; empty strings are explicitly handled (no crash), confirming empty-string items are valid input.
> icu4c/source/test/intltest/listformattertest.cpp:850-858 (`Test21871`): uses `UnicodeString strings[] = {{u"A"}, {u""}};` — empty-string item is a supported/tested input.

## Other entries: call protocol and combination with the target

### icu::ListFormatter::createInstance (3 overloads)
**Claim:** Static factory, must be called (and checked for success) before
`format`/`formatStringsToValue`. Three overloads:
- `createInstance(UErrorCode&)`: uses default locale; delegates to
  `createInstance(Locale, UErrorCode&)`.
- `createInstance(const Locale&, UErrorCode&)`: delegates to
  `createInstance(locale, ULISTFMT_TYPE_AND, ULISTFMT_WIDTH_WIDE, errorCode)`.
- `createInstance(const Locale&, UListFormatterType, UListFormatterWidth, UErrorCode&)`:
  maps `(type,width)` to a CLDR style string via `typeWidthToStyleString`;
  returns `nullptr` with `U_ILLEGAL_ARGUMENT_ERROR` for unmapped combos (none
  currently exist among the 3×3 enum combinations actually in the headers,
  but defensive code exists for a `default:` fallthrough per width).
  Returns `ListFormatter*` (caller owns it — must `delete` it, typically via
  `LocalPointer`). On failure returns `nullptr` and sets `errorCode`.
**Evidence:**
> icu4c/source/i18n/listformatter.cpp:505-522.
> icu4c/source/i18n/unicode/listformatter.h:175,186,198 (declarations/docs: "@return Pointer to a ListFormatter object ... errorCode set if no data available").
**Combination with target:** In the harness, call one `createInstance`
overload (varying locale string and/or type/width per fuzz input), check
`U_SUCCESS`, then feed the resulting pointer's `format`/`formatStringsToValue`
with fuzz-derived `items[]`/`n_items`; destroy the formatter at the end
(`delete` or scope-exit of a smart pointer).

### icu::ListFormatter::formatStringsToValue
**Claim:** Alternate entry to the same core as `format`, but returns a
`FormattedList` by value (move-only type) instead of writing into a
caller-supplied `UnicodeString&`. Same prerequisites as `format` (a
successfully-created `ListFormatter`). Parameters: `items[]`, `n_items`,
`errorCode` (no `appendTo`). Return value: `FormattedList` — check
`U_SUCCESS(errorCode)` before using; on failure `toString`/`appendTo`/`nextPosition`
return guard values (bogus string / unmodified appendable / false) rather
than crashing, per `UPRV_FORMATTED_VALUE_METHOD_GUARD`. Nothing to free
explicitly — destructor handles cleanup when the `FormattedList` goes out of
scope.
**Evidence:**
> icu4c/source/i18n/listformatter.cpp:663-727.
> icu4c/source/i18n/formattedval_impl.h:199-207 (`UPRV_FORMATTED_VALUE_METHOD_GUARD`): returns guard values if `status` already failed or `fData == nullptr`.
**Combination with target:** The harness can call both `format` and
`formatStringsToValue` on the same `ListFormatter` instance with the same or
different `items[]`/`n_items` to exercise both the plain-string path and the
field-position-exposing path; results of `formatStringsToValue` should then
be passed into `toString`/`appendTo`/`nextPosition` to reach the consumer
code (per P2.9).

### icu::FormattedList::toString / appendTo / nextPosition
**Claim:** These are post-processing consumers of `formatStringsToValue`'s
result; they do not belong before the target but complete its "Result Usage"
chain (P2.9). `toString(UErrorCode&)`/`toTempString(UErrorCode&)` materialize
the full formatted `UnicodeString`. `appendTo(Appendable&, UErrorCode&)`
streams the same content into an arbitrary `Appendable` (this is exactly
what `ListFormatter::format` itself does internally via
`UnicodeStringAppendable`, so calling `format` already exercises this path
indirectly). `nextPosition(ConstrainedFieldPosition&, UErrorCode&)` must be
called in a loop, typically after constraining the category/field via
`cfpos.constrainCategory(UFIELD_CATEGORY_LIST)` or
`cfpos.constrainField(UFIELD_CATEGORY_LIST, ULISTFMT_ELEMENT_FIELD)`, until
it returns `false`.
**Evidence:**
> icu4c/source/i18n/unicode/listformatter.h:118-128 (declarations, all `override` of `FormattedValue`).
> icu4c/source/i18n/listformatter.cpp:253 `UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL(FormattedList)` (macro-generated bodies, see formattedval_impl.h:211-244).
> icu4c/source/test/intltest/listformattertest.cpp:66-99, 804-826, 846-858 — all three consumer methods exercised in project tests as shown in P2.9.
**Combination with target:** Call these only on a `FormattedList` obtained
from `formatStringsToValue` on a successfully constructed `ListFormatter`;
no explicit "close" needed, object is destroyed by normal C++ scoping.

## Build notes
**Claim:** Fuzzers for this project are C++ (compiled with `$CXX`/libFuzzer),
linked against the static ICU libraries `icui18n`, `icuuc`, `icutu`,
`icudata` built by the same `build.sh`. The target's header
(`unicode/listformatter.h`) lives under `icu4c/source/i18n/unicode/`, and the
include path `-I$SRC/icu/icu4c/source/i18n` added at compile time resolves
`#include "unicode/listformatter.h"`; `-I$SRC/icu/icu4c/source/common` is
also added for common headers (e.g. `unicode/unistr.h`, `unicode/locid.h`
live under `icu4c/source/common/unicode/`). All files under
`icu4c/source/test/fuzzer/*_fuzzer.cpp` are auto-discovered and each compiled
into its own libFuzzer binary; `list_format_fuzzer.cpp` is the pre-existing
(currently stubbed/baseline) fuzzer name associated with this target.
**Evidence:**
> ossfuzz_view/icu-c2/build.sh:40-42: `$CXX $CXXFLAGS -c $SRC/icu/icu4c/source/test/fuzzer/locale_util.cpp -I$SRC/icu/icu4c/source/common -I$SRC/icu4c/source/test/fuzzer`
> ossfuzz_view/icu-c2/build.sh:44-54: `FUZZERS=$FUZZER_PATH/*_fuzzer.cpp ... $CXX $CXXFLAGS $fuzzer -o $OUT/${file/.cpp/} locale_util.o -I$SRC/icu/icu4c/source/common -I$SRC/icu/icu4c/source/i18n -L$WORK/icu/lib $LIB_FUZZING_ENGINE -licui18n -licuuc -licutu -licudata`
> icu4c/source/test/fuzzer/list_format_fuzzer.cpp:1-17: existing (stubbed) fuzzer file for this exact target, matched by the `*_fuzzer.cpp` glob — confirms the expected file naming/location for the new harness.
> icu4c/source/i18n/unicode/listformatter.h path confirms header is under `i18n/unicode/`, matching the `-I...source/i18n` include flag (so `#include "unicode/listformatter.h"` resolves correctly).
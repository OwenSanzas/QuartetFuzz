## target: icu::Locale::addLikelySubtags

### Declaration / definition
`unicode/locid.h:547`: `U_COMMON_API void addLikelySubtags(UErrorCode& status);` — member of `icu::Locale`, non-static, mutates `*this`.
Definition at `icu4c/source/common/locid.cpp:2186-2205`:
```
void Locale::addLikelySubtags(UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    CharString maximizedLocaleID = ulocimp_addLikelySubtags(getName(), status);
    if (U_FAILURE(status)) {
        if (status == U_MEMORY_ALLOCATION_ERROR) { setToBogus(); }
        return;
    }
    init(maximizedLocaleID.data(), /*canonicalize=*/false);
    if (isBogus()) { status = U_ILLEGAL_ARGUMENT_ERROR; }
}
```

### P2.1 Init Sequence
**Claim:** The `Locale` object must already exist and have a non-failing `status`. No separate ICU global init call is required for `addLikelySubtags` itself (ICU common data must be linked/loadable, which happens automatically via `-licudata`). The caller must construct the `Locale` first (via a constructor, `forLanguageTag`, `createFromName`, `createCanonical`, or `LocaleBuilder`) *before* calling `addLikelySubtags`.
**Evidence:**
> locid.cpp:2187-2189: `if (U_FAILURE(status)) { return; }` — status must be `U_ZERO_ERROR` (or any success code) on entry, otherwise the call is a no-op.
> test/intltest/loctest.cpp:1864-1870: `const Locale min("sv"); Locale result(min); result.addLikelySubtags(status);` — Locale constructed first, then addLikelySubtags called on it.
> test/intltest/loctest.cpp:1836-1837: `Locale result = Locale::forLanguageTag(inputTag, status); result.addLikelySubtags(status);` — forLanguageTag used as the constructing step before addLikelySubtags.

### P2.2 Parameter Construction
**Claim:** Single parameter `UErrorCode& status`, a stack-allocated enum/int reference, must be initialized to `U_ZERO_ERROR` before the call (ICU convention, not specific to this function). No other parameters; the "input" is implicit — it is `this->getName()`, i.e. whatever locale ID string is currently stored in the `Locale` object.
**Evidence:**
> locid.cpp:2192: `CharString maximizedLocaleID = ulocimp_addLikelySubtags(getName(), status);` — input taken from `getName()`, not from an explicit argument.
> loctest.cpp:1832: `IcuTestErrorCode status(*this, "TestPseudoLocales()");` then reused across many calls (test harness wraps UErrorCode); plain production code uses `UErrorCode status = U_ZERO_ERROR;`.

### P2.3 Object Lifecycle
**Claim:** Create (via constructor/forLanguageTag/createFromName/createCanonical/LocaleBuilder) → optionally `canonicalize()` → call `addLikelySubtags(status)` (mutates object in place, no separate "configure" step) → use result via getters (`getName`, `getLanguage`, `getScript`, `getCountry`, `getVariant`, `getBaseName`) or `toLanguageTag()` → destroy via normal C++ stack/heap destructor (Locale has a plain destructor, no explicit `close`/`destroy` API).
**Evidence:**
> locid.cpp:2201: `init(maximizedLocaleID.data(), /*canonicalize=*/false);` — the object is reinitialized in place; same object, no new allocation returned to caller.
> unicode/locid.h: no `close()`/`destroy()` method declared for `Locale`; it's a value type (copyable, `Locale::Locale(const Locale&) = default;` at locid.cpp:594).

### P2.4 Return Value Handling
**Claim:** `addLikelySubtags` returns `void`; success/failure is reported solely through `status`. Caller must check `U_FAILURE(status)` after the call. On `U_MEMORY_ALLOCATION_ERROR` the object is forced to a "bogus" state (`setToBogus()`); on malformed/non-well-formed locale ID, `status` is set to `U_ILLEGAL_ARGUMENT_ERROR` and the object becomes bogus (`isBogus()==true`). If maximization finds no data, "the Locale will be unchanged" (per doc) and `status` stays success.
**Evidence:**
> locid.cpp:2194-2199: `if (U_FAILURE(status)) { if (status == U_MEMORY_ALLOCATION_ERROR) { setToBogus(); } return; }`
> locid.cpp:2202-2204: `if (isBogus()) { status = U_ILLEGAL_ARGUMENT_ERROR; }`
> unicode/locid.h:542-545: doc "@param status error information if maximizing this Locale failed. If this Locale is not well-formed, the error code is U_ILLEGAL_ARGUMENT_ERROR."
> loctest.cpp:4146-4147: `res.addLikelySubtags(status); status.errIfFailureAndReset(...)` — real test code checks/clears status after every call.

### P2.5 Cleanup Sequence
**Claim:** No manual free/close required for `Locale` itself — RAII value type. If the fuzz harness uses `ByteSink`/`std::string` for `toLanguageTag`, no manual free needed there either (std::string manages its own memory). The only "resource" owned indirectly is ICU's static data (loaded once, not per-call) — not freed by the harness.
**Evidence:**
> unicode/locid.h class declaration has no `close`/`~Locale` override shown beyond default destructor (value semantics throughout file, e.g. copy/move ctors at locid.cpp:594-595).
> loctest.cpp: in all usages (`Locale result(min); result.addLikelySubtags(status);`), `result` goes out of scope normally at end of test function — no explicit destroy call anywhere.

### P2.6 API Existence
**Claim:** All referenced functions/symbols exist in the current tree.
**Evidence:**
> `Locale::addLikelySubtags` — unicode/locid.h:547, defined locid.cpp:2186.
> `Locale::minimizeSubtags(UErrorCode&)` — unicode/locid.h:578, defined locid.cpp:2207; overload `minimizeSubtags(bool favorScript, UErrorCode&)` — unicode/locid.h:1142 (under `U_HIDE_INTERNAL_API`... actually public `@stable ICU 63`), defined locid.cpp:2211.
> `Locale::forLanguageTag` — unicode/locid.h:413, defined locid.cpp:2254.
> `Locale::createFromName(const char*)` — unicode/locid.h:452, defined locid.cpp:2312; `createFromName(StringPiece)` locid.cpp:2325 (internal API).
> `Locale::createCanonical` — unicode/locid.h:467, defined locid.cpp:2332.
> `Locale::canonicalize` — unicode/locid.h:586, defined locid.cpp:2232.
> `Locale::toLanguageTag(ByteSink&, UErrorCode&)` — unicode/locid.h:428, defined locid.cpp:2297; template `toLanguageTag<StringClass>` at locid.h:1330-1337 (inline wrapper calling the ByteSink overload).
> Core: `ulocimp_addLikelySubtags` — common/ulocimp.h:288/321, implemented common/loclikely.cpp:288-308; `ulocimp_minimizeSubtags` — common/ulocimp.h:326/361, loclikely.cpp:323-345; `LikelySubtags::makeMaximizedLsrFrom` — common/loclikelysubtags.cpp:521.

### P2.7 Co-call Constraints
**Claim:** No strict lock/unlock-style pairing. The natural pairing is `addLikelySubtags` / `minimizeSubtags` as inverse operations (round-trip tested in the project's own tests), and `forLanguageTag` (build) / `toLanguageTag` (serialize) as a read/write pair. There is no begin/end or open/close requirement specific to `addLikelySubtags`.
**Evidence:**
> loctest.cpp:1888 `LocaleTest::TestAddLikelyAndMinimizeSubtags()` — tests round-trip `from -> add -> expected max`, `from -> minimize -> expected min`, confirming the two are treated as a complementary pair but each is independently callable (not required to call the other).
> loctest.cpp:5965-6021: `actualMax.addLikelySubtags(...)`, `actualMin.minimizeSubtags(...)`, `actualMinFavorScript.minimizeSubtags(...)` called on independently-constructed copies, not chained on the same object — confirms no hard ordering constraint between them.

### P2.8 Prerequisite State
**Claim:** The `Locale` object must not be in a state where `status` already indicates failure (checked at entry). The object's internal `name`/`baseName` must already be populated by a prior constructor/`init()` call (every `Locale` constructor calls `init()` internally, so this is automatically satisfied once a `Locale` exists, including the default constructor which calls `init(nullptr, false)`, locid.cpp:465-469). ICU's locale/likely-subtags data (loclikelysubtags.txt → compiled data) must be loadable at runtime, which happens via the statically linked `-licudata` (see Build notes).
**Evidence:**
> locid.cpp:465-469: `Locale::Locale() : UObject(), payload() { init(nullptr, false); }` — even the no-arg constructor initializes baseName/name.
> locid.cpp:2187-2189 in addLikelySubtags: first check is `U_FAILURE(status)`.

### P2.9 Result Usage
**Claim:** After `addLikelySubtags` mutates the `Locale` in place, callers read the result through the standard `Locale` getters (`getName`, `getLanguage`, `getScript`, `getCountry`, `getVariant`, `getBaseName`) or serialize it with `toLanguageTag`. It is also legal (and tested) to immediately call `minimizeSubtags` on the same maximized object, or to compare `getName()` against an expected string.
**Evidence:**
> loctest.cpp:1870: `assertEquals("addLikelySubtags", max.getName(), result.getName());` — getName() used to read back the maximized ID.
> loctest.cpp:5965-5980 (`TestCanonicalization`/likely-subtags helper, exact fn around line 5960): `actualMax.addLikelySubtags(*pErrorCode); ... actualMax.getName() ...` compared against expected maximized string.
> cldrtest.c and uscript.cpp (`icu4c/source/common/uscript.cpp:137`): `icu::CharString likely = ulocimp_addLikelySubtags(nameOrAbbrOrLocale, internalErrorCode);` then the resulting string is parsed further by uscript code — showing the C-level sibling API's result consumed by string parsing, same pattern as `Locale::addLikelySubtags`'s internal use of `ulocimp_addLikelySubtags`.
> locid.cpp:1519-1524 (`AliasReplacer::replaceTerritory`): builds a `Locale` via `LocaleBuilder`, calls `l.addLikelySubtags(status)`, then reads `l.getCountry()` — demonstrates getCountry() as a direct consumer of the maximized result.

### P2.10 Behaviour Controls
**Claim:** `addLikelySubtags` itself takes no behavior flag; the only control is the content of the locale-ID string it maximizes from (`getName()`), which is influenced by how the `Locale` was built (language/script/region/variant/keyword/private-use subtags) — including special pseudolocale codes (`en-XA`/`ar-XB`/variants `PSACCENT`/`PSBIDI`/`PSCRACK`) that select a different "pseudolocale" lookup path inside the core (`LikelySubtags`/trie walk). `minimizeSubtags` has an explicit `favorScript` bool (default `false` via the 1-arg overload) selecting a different minimization branch.
**Evidence:**
> locid.h:1142: `U_COMMON_API void minimizeSubtags(bool favorScript, UErrorCode& status);` and locid.cpp:2208-2209: `void Locale::minimizeSubtags(UErrorCode& status) { Locale::minimizeSubtags(false, status); }` — favorScript flag default false.
> loctest.cpp:5927-5932: a test subclass `void minimizeSubtags(UErrorCode& status) { Locale::minimizeSubtags(true, status); }` exercising the `favorScript=true` branch.
> loctest.cpp:1810-1828 (TestPseudoLocales test data, e.g. `"und-US-PSACCENT" -> "en-Latn-US-psaccent"`, `"und-EG-PSACCENT" -> "ar-Arab-EG-psaccent"`) — pseudolocale variant codes select different maximization output branch, exercised by feeding different locale IDs (not a parameter of addLikelySubtags, but of the input string).
> loctest.cpp:1830: `std::string extensions("-u-nu-Deva-hc-h23-fw-mon-mu-celsius-x-somethin-more");` appended to tags to test that trailing `-u-`/`-x-` extension/private-use data round-trips through maximize.

### P2.11 Input Expectations
**Claim:** The implicit input is `this->getName()` — a NUL-terminated ICU-canonical-form locale ID string (`lang_SCRIPT_REGION@keywords`, underbar-separated, produced by whatever constructor/`init()` built the object). The function canonicalizes it again internally (`ulocimp_canonicalize`) before the trie lookup. If the locale ID is not well-formed, `status` becomes `U_ILLEGAL_ARGUMENT_ERROR` and the object becomes bogus; there is no explicit documented byte-size limit on `addLikelySubtags` itself, but `Locale`'s own constructors cap component sizes at `ULOC_STRING_LIMIT` (357913941) and use growable heap buffers (`CharString`/`FixedString`) so no fixed small-buffer truncation occurs inside `addLikelySubtags`.
**Evidence:**
> locid.cpp:2192: `ulocimp_addLikelySubtags(getName(), status)` — input is `getName()`, a `const char*` (NUL-terminated).
> loclikely.cpp:298-308: `ulocimp_addLikelySubtags` calls `ulocimp_canonicalize(localeID, status)` then `_uloc_addLikelySubtags(localeBuffer.data(), sink, status)` — re-canonicalizes before maximizing.
> unicode/locid.h:542-545: "@param status ... If this Locale is not well-formed, the error code is U_ILLEGAL_ARGUMENT_ERROR."
> locid.cpp:505-508: `lsize = uprv_strlen(newLanguage); if (lsize < 0 || lsize > ULOC_STRING_LIMIT) { return; }` (constructor-side length guard, not inside addLikelySubtags but upstream of it).
> loclikelysubtags.cpp:521-526: `LSR LikelySubtags::makeMaximizedLsrFrom(const Locale &locale, ...)` sets `errorCode = U_ILLEGAL_ARGUMENT_ERROR` for malformed locale (core-level check reached via addLikelySubtags -> ulocimp_addLikelySubtags -> _uloc_addLikelySubtags -> makeMaximizedLsrFrom).

## Other Logic-Group entries: call protocol and combination with the target

### icu::Locale::minimizeSubtags(UErrorCode&) and minimizeSubtags(bool favorScript, UErrorCode&)
**Exists:** Yes — unicode/locid.h:578 (1-arg) and locid.h:1142 (2-arg, `favorScript`), defined locid.cpp:2207-2230.
**Before it:** A constructed `Locale` with success `status` (same precondition as addLikelySubtags); commonly called on a locale that has *already* been maximized (its own doc: "the minimization algorithm relies on proper maximization" — locid.h:558-560), but it is legal to call directly on any locale since it re-maximizes internally to verify (per Logic-Group description: "minimization re-maximizes to verify").
**Parameters/flags:** `status` (UErrorCode&, in/out, must be success on entry — same check at locid.cpp:2213/2213-2215); `favorScript` (bool) — `false` keeps region preference over script, `true` selects the alternate branch favoring script in ambiguous cases (loctest.cpp:5927-5932 "TestAddLikelyAndMinimizeSubtags"/favorScript test subclass).
**Return:** void; errors via `status`, bogus-on-`U_MEMORY_ALLOCATION_ERROR` (locid.cpp:2219-2222) and `U_ILLEGAL_ARGUMENT_ERROR` if result is bogus (locid.cpp:2226-2229) — identical pattern to addLikelySubtags.
**Free:** None (same RAII value-type semantics as `Locale`).
**Combination with target:** Natural round-trip partner — call `addLikelySubtags(status)` then `minimizeSubtags(status)` (or vice versa) on independent `Locale` copies to exercise both directions of the CLDR likely-subtags machinery; see loctest.cpp:1888-... `TestAddLikelyAndMinimizeSubtags`, and loctest.cpp:5965-6021 which calls both on copies (`actualMax`, `actualMin`, `actualMinFavorScript`) of the same base `Locale`.
**Evidence:**
> locid.cpp:2207-2209: `void Locale::minimizeSubtags(UErrorCode& status) { Locale::minimizeSubtags(false, status); }`
> locid.cpp:2217: `CharString minimizedLocaleID = ulocimp_minimizeSubtags(getName(), favorScript, status);`
> loctest.cpp:1882-1884: `result.minimizeSubtags(status); ... assertEquals("minimizeSubtags", min.getName(), result.getName());`

### icu::Locale::forLanguageTag(StringPiece tag, UErrorCode& status)
**Exists:** Yes — unicode/locid.h:413, static factory, defined locid.cpp:2254-2295.
**Before it:** Nothing — it is itself a *source*/constructor-equivalent (static, returns a new `Locale` by value). `status` must be success on entry (checked at locid.cpp:2259-2261).
**Parameters:** `tag` — a `StringPiece` (pointer+length, need not be NUL-terminated) holding a raw BCP-47 string; accepts extensions (`-u-...`), private-use (`-x-...`), legacy/grandfathered tags (e.g. `en-GB-oed`) per doc locid.h:392-412. Internally calls `ulocimp_forLanguageTag(tag.data(), tag.length(), &parsedLength, status)` (locid.cpp:2275-2279); if `parsedLength != tag.size()` (trailing ill-formed subtags), sets `U_ILLEGAL_ARGUMENT_ERROR` and returns a bogus Locale (locid.cpp:2285-2288).
**Return:** `Locale` by value (always returns an object — bogus on failure, `result(Locale::eBOGUS)` initial at locid.cpp:2257). Must check `U_FAILURE(status)` and/or `result.isBogus()` before further use.
**Free:** None — stack value, destructor handles cleanup.
**Combination with target:** Primary realistic "source" for addLikelySubtags in this feature's own tests: `Locale result = Locale::forLanguageTag(inputTag, status); result.addLikelySubtags(status);` (loctest.cpp:1836-1838, repeated at 1847-1849 with an extended tag). This is the pattern to replicate in a harness: build via `forLanguageTag` from fuzz bytes, then call the target.
**Evidence:**
> locid.cpp:2255-2295 full body quoted above (steps: check status → call `ulocimp_forLanguageTag` → check parsedLength covers whole input → `result.init(localeID.data(), false)` → check bogus).
> loctest.cpp:1836-1837, 1847-1848 (TestPseudoLocales) — real call-site chain.

### icu::Locale::createFromName(const char* name) / createFromName(StringPiece name)
**Exists:** Yes — unicode/locid.h:452 (`const char*` overload, public stable ICU 2.0) and locid.h:456 (`StringPiece` overload, marked `@internal`, under `U_HIDE_INTERNAL_API`). Defined locid.cpp:2312-2330.
**Before it:** Nothing (static factory). `name == nullptr` returns `getDefault()` (the process default Locale) instead of an empty locale (locid.cpp:2315-2322).
**Parameters:** `name` — raw locale-ID C string ("minimal canonicalization as per uloc_getName()", doc locid.h:444-445), e.g. POSIX-style `lang_SCRIPT_REGION@keywords`. No canonicalization pass (`init(name, false)` — `canonicalize=false`, locid.cpp:2317/2328).
**Return:** `Locale` by value; no explicit error code parameter on the `const char*` overload — malformed names simply produce a bogus `Locale` (checkable via `isBogus()`), there is no `status` out-param here to check.
**Free:** None (value type).
**Combination with target:** `Locale loc = Locale::createFromName(fuzzedCString); loc.addLikelySubtags(status);` — exercises the "classic ICU locale ID" parsing path distinctly from `forLanguageTag`'s BCP-47 parser, per the Logic-Group description. Not found as a direct call site paired with `addLikelySubtags` in loctest.cpp/cloctst.c searched; combination is by design (both are constructors feeding the same core maximize pipeline through `getName()`).
**Evidence:**
> locid.cpp:2312-2323: `Locale Locale::createFromName(const char *name) { if (name) { Locale l(""); l.init(name, false); return l; } else { return getDefault(); } }`
> unicode/locid.h:443-451 doc: "Creates a locale which has had minimal canonicalization as per uloc_getName()."

### icu::Locale::createCanonical(const char* name)
**Exists:** Yes — unicode/locid.h:467, defined locid.cpp:2332-2337.
**Before it:** Nothing (static factory). `name` must not be `nullptr` per doc (locid.h:462 "Must not be nullptr") — unlike `createFromName`, no null-check in the body (`Locale loc(""); loc.init(name, true); return loc;`, locid.cpp:2333-2336) so passing `nullptr` would dereference in `init`/`uloc_canonicalize` — caller (fuzz harness) must guard against passing NULL.
**Parameters:** `name` — raw locale ID string; internally canonicalizes via `uloc_canonicalize()` (doc locid.h:461,465) through `init(name, /*canonicalize=*/true)`.
**Return:** `Locale` by value; bogus on malformed input (check `isBogus()`), no `status` out-param.
**Free:** None.
**Combination with target:** `Locale loc = Locale::createCanonical(fuzzedCString); loc.addLikelySubtags(status);` exercises the canonicalization-then-maximize path (different normalization from `createFromName`'s `canonicalize=false`), per Logic-Group notes on this entry.
**Evidence:**
> locid.cpp:2332-2337 quoted above.
> unicode/locid.h:459-467 doc.

### icu::Locale(const char* language, const char* country, const char* variant, const char* keywordsAndValues)
**Exists:** Yes — constructor, defined locid.cpp:482-... (body continues past the excerpt read: sizes checked against `ULOC_STRING_LIMIT`, components concatenated with `_`/`@`).
**Before it:** Nothing; this is itself the object-creation step.
**Parameters:** Four independent `const char*` components; all may be `nullptr` individually (only all-three-null `language/country/variant` triggers the `init(nullptr,false)` shortcut, locid.cpp:488-491); each non-null component's length is bounds-checked against `ULOC_STRING_LIMIT` (357913941) and if `lsize/csize/vsize > ULOC_STRING_LIMIT` the constructor returns early leaving the object default/bogus (locid.cpp:505-508 shown for language; same pattern for country/variant).
**Return:** N/A (constructor). Resulting object may be bogus if construction failed internally — check via `isBogus()` before use.
**Free:** None (value type, stack-constructible).
**Combination with target:** This is exactly `locale_fuzzer.cpp`'s existing pattern (`icu::Locale locale(language.c_str(), country.c_str(), variant.c_str(), kv_pairs.c_str());`, test/fuzzer/locale_fuzzer.cpp:46-47) — extend it with `locale.addLikelySubtags(status);` to combine fuzzed 4-subtag construction with the target, letting the fuzzer independently control language/country/variant/keyword bytes before maximization, per the Logic-Group's explicit guidance for this entry.
**Evidence:**
> locid.cpp:482-520 (constructor body, quoted above through line 520).
> test/fuzzer/locale_fuzzer.cpp:41-48 (existing project fuzzer already builds `Locale` this way from 4 length-prefixed fuzz substrings, but currently does NOT call addLikelySubtags — a candidate extension point, not itself evidence of pairing with the target).

### icu::Locale::canonicalize(UErrorCode& status)
**Exists:** Yes — unicode/locid.h:586, defined locid.cpp:2232-2252.
**Before it:** A constructed `Locale` (any state) and success `status`; additionally the object must not already be bogus — the function explicitly rejects bogus input: `if (isBogus()) { status = U_ILLEGAL_ARGUMENT_ERROR; return; }` (locid.cpp:2237-2240).
**Parameters:** `status` only; operates in place on `getName()`.
**Return:** void; errors via `status` — same `U_MEMORY_ALLOCATION_ERROR`→bogus and `isBogus()`→`U_ILLEGAL_ARGUMENT_ERROR` pattern as addLikelySubtags/minimizeSubtags (locid.cpp:2243-2251).
**Free:** None.
**Combination with target:** Call `loc.canonicalize(status)` before `loc.addLikelySubtags(status)` to feed the target a differently-normalized string (per Logic-Group note: "changes the string addLikelySubtags will re-parse, exposing different alias/trailing-data edge cases"). Direct call-site pairing with addLikelySubtags not found in tests searched (`canonicalize` and `addLikelySubtags` tested separately in loctest.cpp), but both operate on the same `getName()`/`init()` machinery so chaining is valid and type-safe.
**Evidence:**
> locid.cpp:2232-2252 full body.
> unicode/locid.h:580-586 doc: "Canonicalize the locale ID of this object according to CLDR."

### icu::Locale::toLanguageTag(ByteSink& sink, UErrorCode& status) const  [and template toLanguageTag<StringClass>(UErrorCode&)]
**Exists:** Yes — unicode/locid.h:428 (ByteSink overload), defined locid.cpp:2297-2310; template wrapper at locid.h:1330-1337 (`inline StringClass Locale::toLanguageTag(UErrorCode& status) const { ... toLanguageTag(sink, status); }`).
**Before it:** A constructed (optionally maximized) `Locale`; rejects bogus locales: `if (isBogus()) { status = U_ILLEGAL_ARGUMENT_ERROR; return; }` (locid.cpp:2304-2307).
**Parameters:** `sink` — caller-provided `ByteSink&` (e.g. `StringByteSink<std::string>`) that receives the BCP-47 output bytes; `status` as usual. Template overload internally constructs a `StringByteSink<StringClass>` and returns the populated string/StringClass by value (locid.h:1330-1337).
**Return:** void for ByteSink overload (output via `sink`); `StringClass` by value for the template overload. "If this function fails, partial output may have been written to the sink" (locid.h:421) — caller must still check `status` even if some bytes were written.
**Free:** None for `std::string`-backed sinks (RAII); if using a raw buffer sink, caller owns that buffer's lifetime.
**Combination with target:** Consumer step — after `addLikelySubtags`, serialize with `toLanguageTag` to validate the maximized locale prints without OOB/UB (per Logic-Group note: "exercising the output side of the round trip"). Pattern: `loc.addLikelySubtags(status); std::string tag = loc.toLanguageTag<std::string>(status);` or `StringByteSink<std::string> sink(&out); loc.toLanguageTag(sink, status);`.
**Evidence:**
> locid.cpp:2297-2310 (ByteSink overload body).
> locid.h:1325-1337 (template overload body, shown via earlier read: `StringClass Locale::toLanguageTag(UErrorCode& status) const { StringClass result; StringByteSink<StringClass> sink(&result); toLanguageTag(sink, status); return result; }` — approximate, confirmed present at locid.h:1331/1336 matches).
> loctest.cpp:6241-6243: `loc_c.toLanguageTag(sink, status); status.errIfFailureAndReset(...); assertEquals(loc_c.getName(), tag_c, result.c_str());` — real usage with `StringByteSink<std::string>`.
> loctest.cpp:5965-5980 region (helper test function around line 5960s) also calls `toLanguageTag` immediately after `addLikelySubtags`/`minimizeSubtags` on the same object to print/compare results (error messages reference "toLanguageTag(%s) return error" at loctest.cpp:5974, 5995, 6016).

## Build notes

**Claim:** Fuzzers for this project are plain C++ translation units named `*_fuzzer.cpp` under `icu4c/source/test/fuzzer/`, each compiled and linked independently against the statically-built ICU libraries; `unicode/locid.h` is reachable via `-I$SRC/icu/icu4c/source/common` (it lives at `icu4c/source/common/unicode/locid.h`, i.e. under the `common` include root as `unicode/locid.h`).

**Evidence:**
> ossfuzz_view/icu-c3/build.sh:44-46:
> ```
> FUZZER_PATH=$SRC/icu/icu4c/source/test/fuzzer
> # Assumes that all fuzzers files end with'_fuzzer.cpp'.
> FUZZERS=$FUZZER_PATH/*_fuzzer.cpp
> ```
> build.sh:48-54:
> ```
> for fuzzer in $FUZZERS; do
>   file=${fuzzer:${#FUZZER_PATH}+1}
>   $CXX $CXXFLAGS \
>     $fuzzer -o $OUT/${file/.cpp/} locale_util.o \
>     -I$SRC/icu/icu4c/source/common -I$SRC/icu/icu4c/source/i18n -L$WORK/icu/lib \
>     $LIB_FUZZING_ENGINE -licui18n -licuuc -licutu -licudata
> done
> ```
> build.sh:24-30 shows ICU itself is configured/built first with static libs (`--with-data-packaging=static --enable-static --disable-shared`) via `runConfigureICU Linux`, so `-licudata` provides the CLDR likely-subtags trie data that `addLikelySubtags` needs at runtime (no separate data-file loading step needed by the harness).
> Existing project fuzzer `icu4c/source/test/fuzzer/locale_fuzzer.cpp:13`: `#include "unicode/locid.h"` confirms the include path resolves `unicode/locid.h` relative to the `common` include root passed via `-I$SRC/icu/icu4c/source/common`.
> `locale_morph_fuzzer.cpp` (current repo state, lines 1-16) is a disabled/stubbed harness that only keeps `icu::Locale::addLikelySubtags` linked via an `__asm__`-named extern symbol and does not actually invoke it — confirming this is the designated target slot for the new harness but giving no behavioural pattern to copy; the real usage patterns were instead taken from `locale_fuzzer.cpp` (existing live fuzzer using the 4-arg `Locale` constructor) and from `test/intltest/loctest.cpp` call sites documented above.
> Language/toolchain: C++ (icu4c `common`/`i18n` libraries), compiled with `$CXX`/`$CXXFLAGS` set by OSS-Fuzz environment plus ICU's own macros `-DU_CHARSET_IS_UTF8=1 -DU_USING_ICU_NAMESPACE=0 -DU_ENABLE_DYLOAD=0 -DU_USE_STRTOD_L=0` (build.sh:24).
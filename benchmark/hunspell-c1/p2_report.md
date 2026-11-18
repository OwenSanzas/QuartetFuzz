## target: Hunspell::analyze

### P2.1 Init Sequence
**Claim:** A `Hunspell` object must be constructed (loading an `.aff`/`.dic` pair) before `analyze()` can return anything useful; construction fully initializes the hash/affix/suggest managers synchronously, there is no separate "open" call.
**Evidence:**
> src/hunspell/hunspell.cxx:2311-2313: `Hunspell::Hunspell(const char* affpath, const char* dpath, const char* key) : m_Impl(new HunspellImpl(affpath, dpath, key)) {}`
> src/hunspell/hunspell.cxx:196-222: `HunspellImpl::HunspellImpl` builds `m_HMgrs` (HashMgr from dpath/affpath), then `pAMgr = std::make_unique<AffixMgr>(affpath, m_HMgrs, key)`, reads `try_string`/`encoding`/`langnum`/`utf8`/`complexprefixes`/`wordbreak` from `pAMgr`, and finally builds `pSMgr = std::make_unique<SuggestMgr>(...)`.
> src/hunspell/hunspell.cxx:1701-1702 (inside `analyze_internal`): `if (!pSMgr || m_HMgrs.empty()) return slst;` — analyze silently returns an empty vector if the managers were never built (defensive, but construction above always populates them in the current code).
> src/hunspell/hunspell.hxx:126: constructor signature `Hunspell(const char* affpath, const char* dpath, const char* key = nullptr);`

### P2.2 Parameter Construction
**Claim:** `analyze` takes a single `const std::string& word` — any byte sequence is accepted as input (no required encoding prefix at the API level; encoding is governed by the loaded dictionary's SET/FLAG directives and `utf8` mode). No other parameters; no output parameters (return by value).
**Evidence:**
> src/hunspell/hunspell.hxx:176: `std::vector<std::string> analyze(const std::string& word);`
> src/hunspell/hunspell.cxx:1703-1709: inside `analyze_internal`, length is checked against `MAXWORDUTF8LEN` (utf8 mode) or `MAXWORDLEN` (8-bit mode) and the function returns an empty vector if the word is too long — the caller does not need to pre-truncate, but very long words are a no-op.
> src/hunspell/hunspell.cxx:1732-1735: under `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`, cleaned length `wl` is additionally capped at `32768`.

### P2.3 Object Lifecycle
**Claim:** Create `Hunspell` once (loads `.aff`+`.dic`) → optionally `add_dic()` to extend the dictionary set → call `analyze`/`stem`/`generate`/`spell`/`suggest` any number of times in any order on that single instance → destroy with `delete` (handled by RAII/`~Hunspell`). `analyze` itself is stateless across calls (no begin/end pairing).
**Evidence:**
> src/hunspell/hunspell.cxx:2315-2317: `Hunspell::~Hunspell() { delete m_Impl; }` frees `HunspellImpl`, which in turn owns `m_HMgrs` (vector of `unique_ptr<HashMgr>`), `pAMgr` (`unique_ptr<AffixMgr>`), `pSMgr` (`unique_ptr<SuggestMgr>`) — all cleaned automatically by `HunspellImpl`'s implicit destructor plus the explicit `HunspellImpl::~HunspellImpl` at hunspell.cxx:224-229 (which only additionally frees `csconv` under `MOZILLA_CLIENT`).
> src/tools/analyze.cxx:67: `Hunspell* pMS = new Hunspell(argv[1], argv[2]);` then the loop calls `pMS->spell`, `pMS->analyze`, `pMS->stem`, `pMS->generate` repeatedly on the same object (lines 77-91) with no re-init between calls.
> src/tools/fuzzer.cxx:63-67: constructs one `Hunspell dict(...)` (stack object, destroyed at scope end) then calls `dict.spell`/`dict.suggest` — same single-construct-many-calls pattern used throughout the project's tools.

### P2.4 Return Value Handling
**Claim:** `analyze` returns `std::vector<std::string>` by value; an empty vector means "no analysis" (unknown word, too long, or managers not initialized) — it is not an error code, callers just check `.empty()`.
**Evidence:**
> src/hunspell/hunspell.cxx:1699-1746: all early-return paths in `analyze_internal` return the (still empty) `slst` local — no exception, no sentinel value other than emptiness.
> src/tools/analyze.cxx:87-90: `std::vector<std::string> result = pMS->analyze(buf); for (const auto& i : result) { fprintf(stdout, ...); }` — caller iterates without checking any special "error" case, relying on empty-vector-means-nothing.
> src/hunspell/hunspell.cxx:1942-1946 (`HunspellImpl::generate`): `std::vector<std::string> pl2 = analyze(word);` — consumed directly as the "pattern" input to `suggest_gen`, with no non-empty check before use (empty `pl2` simply yields no suggestions downstream).

### P2.5 Cleanup Sequence
**Claim:** No per-call cleanup is needed for `analyze`'s return value — it is a plain `std::vector<std::string>` owned by the caller via RAII (no manual free). The only cleanup object in the lifecycle is the `Hunspell` instance itself (`delete`), which releases `HashMgr`/`AffixMgr`/`SuggestMgr` resources.
**Evidence:**
> src/hunspell/hunspell.hxx:176-177: the C++ overload returns `std::vector<std::string>` by value (no manual free), while the deprecated `int analyze(char*** slst, const char* word)` overload (hunspell.hxx:177) allocates a C array that the caller would free with the deprecated `free_list` (hunspell.hxx:168) — irrelevant to the target overload.
> src/hunspell/hunspell.cxx:2315-2317: `delete m_Impl` is the only required cleanup call, done once when the `Hunspell` object goes out of scope / is deleted.

### P2.6 API Existence
**Claim:** All functions cited below (`Hunspell::analyze`, `Hunspell::stem` (both overloads), `Hunspell::generate` (both overloads), `Hunspell::add_dic`, `Hunspell::Hunspell`, `Hunspell::~Hunspell`, and the internal `HunspellImpl::analyze`/`analyze_internal`) exist in the current source tree at the cited lines.
**Evidence:**
> src/hunspell/hunspell.hxx:126,132,176,180,189,193,203: declarations of constructor, `add_dic`, `analyze`, `stem(word)`, `stem(morph)`, `generate(word,word2)`, `generate(word,pl)`.
> src/hunspell/hunspell.cxx:2311,2320,2342,2346,2382,2386,2390: matching definitions forwarding to `m_Impl`.
> src/hunspell/hunspell.cxx:1674,1699,1535,1588,1942,1989,232: `HunspellImpl::analyze`, `HunspellImpl::analyze_internal`, `HunspellImpl::stem(vector)`, `HunspellImpl::stem(word)`, `HunspellImpl::generate(word,pl)`, `HunspellImpl::generate(word,pattern)`, `HunspellImpl::add_dic`.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists around `analyze`. The only "paired" lifecycle is construct/destroy of the `Hunspell` object (`new`/`delete` or RAII). `add_dic` has no corresponding "remove_dic"; dictionaries loaded via `add_dic` persist for the lifetime of the `Hunspell` object.
**Evidence:**
> src/hunspell/hunspell.cxx:232-235: `HunspellImpl::add_dic` only pushes a new `HashMgr` onto `m_HMgrs`; no code elsewhere removes entries from `m_HMgrs` except the destructor's implicit vector teardown.
> src/hunspell/hunspell.hxx: no `remove_dic`/`close` declared anywhere in the class (grep of the header shows only `add`, `add_with_flags`, `add_with_affix`, `remove` which operate on words, not dictionary files).

### P2.8 Prerequisite State
**Claim:** `analyze` requires a successfully constructed `Hunspell`/`HunspellImpl` with non-null `pSMgr` and a non-empty `m_HMgrs` (both guaranteed by the constructor in current code, but defensively checked). It also depends on affix-file-derived state set up by `AffixMgr`: `utf8` flag, `langnum`, ICONV/OCONV tables (`pAMgr->get_iconvtable()/get_oconvtable()`), ignore-chars table, and word-break table — all populated once at construction and read (not mutated) by `analyze`.
**Evidence:**
> src/hunspell/hunspell.cxx:1701-1702: `if (!pSMgr || m_HMgrs.empty()) return slst;` guards the prerequisite state.
> src/hunspell/hunspell.cxx:1703-1709: branches on `utf8` (set at hunspell.cxx:214 from `pAMgr->get_utf8()`).
> src/hunspell/hunspell.cxx:1718: `RepList* rl = (pAMgr) ? pAMgr->get_iconvtable() : nullptr;` — ICONV table from the loaded affix file.
> src/hunspell/hunspell.cxx:1677: `RepList* rl = (pAMgr) ? pAMgr->get_oconvtable() : nullptr;` — OCONV table used after `analyze_internal` returns, inside `HunspellImpl::analyze`.
> src/hunspell/hunspell.cxx:1755 (`if (langnum == LANG_hu)`): `langnum`, set once at construction (hunspell.cxx:213), selects the Hungarian numeric/dash-compound special-casing branch.

### P2.9 Result Usage
**Claim:** The vector of strings returned by `analyze` (one line per morphological interpretation, fields separated by `MSEP_FLD`/tagged with `MORPH_STEM`="st:" etc., records separated conceptually by newline-per-vector-entry) is consumed by: (a) `HunspellImpl::stem(const std::string&)` which calls `analyze(word)` then feeds the result straight into `HunspellImpl::stem(const std::vector<std::string>&)` to scan for `MORPH_STEM`/`MORPH_DERI_SFX`/`MORPH_INFL_SFX`/`MORPH_SURF_PFX` tags; (b) `HunspellImpl::generate(word, pattern)` which calls `analyze(pattern)` to turn a pattern word into morph-description lines, then calls the vector-based `generate(word, pl)` overload; (c) `HunspellImpl::generate(word, pl)` itself calls `analyze(word)` to get the base word's own morph breakdown (`pl2`) and passes it to `pSMgr->suggest_gen(pl2, i, ...)` for every description string `i` in `pl`; (d) external tools print/parse the lines directly (string search for tag substrings) as in `chmorph.cxx`.
**Evidence:**
> src/hunspell/hunspell.cxx:1588-1590: `std::vector<std::string> HunspellImpl::stem(const std::string& word) { return stem(analyze(word)); }`
> src/hunspell/hunspell.cxx:1535-1586: `HunspellImpl::stem(const std::vector<std::string>& desc)` scans each line `i` for `MORPH_DERI_SFX`/`MORPH_INFL_SFX` (csutil.hxx:109-110) and `MORPH_SURF_PFX`/`MORPH_STEM` (csutil.hxx:112,103) via `copy_field`.
> src/hunspell/hunspell.cxx:1989-1994: `HunspellImpl::generate(word, pattern)`: `std::vector<std::string> pl = analyze(pattern); std::vector<std::string> slst = generate(word, pl);`
> src/hunspell/hunspell.cxx:1942-1954: `HunspellImpl::generate(word, pl)`: `std::vector<std::string> pl2 = analyze(word); ... pSMgr->suggest_gen(pl2, i, suggest_start)`.
> src/tools/chmorph.cxx:86-103: `std::vector<std::string> pl = pMS->analyze(next); ... strstr(i.c_str(), argv[4]) ... std::vector<std::string> pl2 = pMS->generate(next, pl);` — real-world example of analyze's output text-searched and then fed back into `generate` as the pattern-list argument.
> src/tools/analyze.cxx:87-90: analyze's lines are simply printed with `fprintf`, the simplest "result usage" (display only).

### P2.10 Behaviour Controls
**Claim:** Behaviour of `analyze`/`analyze_internal` is controlled by: (1) `utf8` flag (from the loaded `.aff` file's `SET` directive) selecting UTF-8 vs 8-bit length limits and case-folding routines; (2) `captype` computed by `cleanword2` (NOCAP/INITCAP/ALLCAP/HUHCAP/HUHINITCAP) which is used deeper in `suggest_morph`/`affix_check_morph` (not directly branched on inside the shown `analyze_internal` excerpt but passed down); (3) `langnum == LANG_hu` enabling a numeric/dash-compound pre-parsing branch; (4) presence of ICONV/OCONV replacement tables (`pAMgr->get_iconvtable()/get_oconvtable()`) from the affix file, which rewrite the word before analysis and the output lines after; (5) trailing '.' characters handled as abbreviation count `abbv` by `cleanword2`; (6) `complexprefixes` flag from the affix file changes prefix/suffix matching order inside the deeper `affix_check_morph`/`compound_check_morph` core (not shown here but set once at construction and read throughout). For `generate`, the two overloads select whether the "pattern" side is itself re-analyzed (`generate(word,string)`) or passed pre-split as a vector (`generate(word, vector<string>)`), letting a harness directly supply morph-description strings like `"is:plural"`.
**Evidence:**
> src/hunspell/hunspell.cxx:1703-1709: utf8-based length checks.
> src/hunspell/hunspell.cxx:264-308 (`cleanword2`): computes `*pcaptype` via `get_captype_utf8`/`get_captype`, and `*pabbrev` from trailing '.' characters (lines 285-291).
> src/hunspell/hunspell.cxx:1755 (`if (langnum == LANG_hu)`): Hungarian-specific numeric branch start.
> src/hunspell/hunspell.cxx:1718,1677: ICONV/OCONV table lookups.
> src/hunspell/hunspell.cxx:217: `complexprefixes = pAMgr->get_complexprefixes();` set once from the affix file.
> src/hunspell/hunspell.hxx:193,203: the two `generate` overloads — `generate(word, word2)` re-analyzes `word2` as a pattern; `generate(word, pl)` takes a ready `vector<string>` of morph descriptions.

### P2.11 Input Expectations
**Claim:** `analyze`'s `word` parameter is treated as a single "word" (not a sentence/document): no internal tokenization is performed on it (tokenization, if any, is the caller's job, as shown by `chmorph.cxx`'s `TextParser`/`next_token` loop feeding one token at a time). Expected length is bounded by `MAXWORDUTF8LEN` (utf8 mode) or `MAXWORDLEN` (8-bit mode, default 100, see `hunspell.hxx:90` `#define MAXWORDLEN 100`) — oversized input yields an empty result, not an error. Leading blanks and trailing periods are stripped/recorded by `cleanword2`; IGNORE-listed characters (from the affix file) are stripped first via `clean_ignore`. In fuzzing builds, cleaned length is further capped at 32768.
**Evidence:**
> src/hunspell/hunspell.hxx:90: `#define MAXWORDLEN 100` (fallback definition when not already defined, e.g. by atypes.hxx).
> src/hunspell/hunspell.cxx:1703-1709: `if (utf8) { if (word.size() >= MAXWORDUTF8LEN) return slst; } else { if (word.size() >= MAXWORDLEN) return slst; }`.
> src/hunspell/hunspell.cxx:1732-1735: `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` 32768 cap on cleaned length `wl`.
> src/hunspell/hunspell.cxx:240-254 (`clean_ignore`) and 264-308 (`cleanword2`): leading-blank skip (lines 279-283), trailing-'.' stripping into `abbv` (lines 285-291).
> src/tools/chmorph.cxx:82-86: caller tokenizes a line with `TextParser`/`p->next_token(next)` before calling `pMS->analyze(next)` once per token — confirms analyze expects single tokens, not whole lines/documents.

## Other entries of the Logic Group (how they combine with `analyze` in one harness)

### Hunspell::stem(const std::string& word)
**Claim:** Needs the same constructed `Hunspell` object as `analyze` (same `Hunspell(aff,dic)` prerequisite). Internally it simply calls `analyze(word)` and reduces the result, so it exercises the identical `analyze_internal` path plus extra stem-extraction logic (`MORPH_STEM`/`MORPH_DERI_SFX` tag scanning). Returns `std::vector<std::string>` of stems (plain RAII, nothing to free manually). Empty result on no-match or too-long word, same as `analyze`.
**Evidence:**
> src/hunspell/hunspell.hxx:180: `std::vector<std::string> stem(const std::string& word);`
> src/hunspell/hunspell.cxx:2346-2348: `Hunspell::stem(word) { return m_Impl->stem(word); }`
> src/hunspell/hunspell.cxx:1588-1590: `HunspellImpl::stem(word) { return stem(analyze(word)); }`
> A harness can call `analyze(word)` and `stem(word)` on the same `word` and the same `Hunspell` instance independently or in sequence; they do not need to be called in a specific order relative to each other (each internally reruns `analyze_internal`).

### Hunspell::stem(const std::vector<std::string>& morph)
**Claim:** The two-argument-shaped (vector) overload bypasses `analyze_internal` entirely and only parses morphological tag strings (`MORPH_STEM`, `MORPH_DERI_SFX`, `MORPH_INFL_SFX`, `MORPH_SURF_PFX`) out of caller-supplied lines. It does **not** require those lines to have come from `analyze` — it only requires a `Hunspell`/`HunspellImpl` with `pSMgr` set (for the `MORPH_DERI_SFX` branch, which calls `pSMgr->suggest_gen`), i.e. still needs a successfully constructed `Hunspell`. A harness can feed this overload either the real output of `analyze(word)` (the documented usage, per the header comment) or arbitrary attacker-controlled strings shaped like `"st:xxx"` / `"ds:xxx"` / `"is:xxx"` / `"sp:xxx"` to directly fuzz the tag-parsing/`suggest_gen` path without going through `analyze_internal`.
**Evidence:**
> src/hunspell/hunspell.hxx:183-189: doc comment `"stem(analysis, n) - get stems from a morph. analysis ... int n1 = analyze(&result, "words"); int n2 = stem(&result2, result, n1);"` — documents chaining `analyze` → `stem(vector)`.
> src/hunspell/hunspell.cxx:1535-1586: `HunspellImpl::stem(const std::vector<std::string>& desc)` — uses `pSMgr->suggest_gen` (line 1560) only inside the `MORPH_DERI_SFX` branch, so a non-null `pSMgr` (guaranteed by construction) is the only real prerequisite; it does not call `analyze_internal`.
> src/hunspell/hunspell.cxx:2342-2344: `Hunspell::stem(const std::vector<std::string>& desc) { return m_Impl->stem(desc); }`.

### Hunspell::generate(const std::string& word, const std::string& word2)
**Claim:** Calls `analyze(word2)` internally to build the pattern, then `analyze(word)` again (inside the vector-based `generate` overload) to build the base-word breakdown, then drives `SuggestMgr::suggest_gen` and filters results through `spell()`. Needs the same constructed `Hunspell` object. Returns `std::vector<std::string>` (possibly empty if no generation match or `pSMgr` filtered everything through `spell`).
**Evidence:**
> src/hunspell/hunspell.hxx:193: `std::vector<std::string> generate(const std::string& word, const std::string& word2);`
> src/hunspell/hunspell.cxx:2390-2392: `Hunspell::generate(word,pattern) { return m_Impl->generate(word, pattern); }`
> src/hunspell/hunspell.cxx:1989-1994: `HunspellImpl::generate(word, pattern)`: `pl = analyze(pattern); slst = generate(word, pl); uniqlist(slst);`
> src/hunspell/hunspell.cxx:1942-1987: `HunspellImpl::generate(word, pl)`: `pl2 = analyze(word); ... pSMgr->suggest_gen(pl2, i, ...) ... spell(*it, candidate_stack, nullptr, nullptr, suggest_start)` — note `generate` depends on `spell_internal`/`spell` (one of the Logic Group's "Core" functions) to filter candidate outputs, so a harness exercising `generate` transitively exercises `HunspellImpl::spell_internal` too.

### Hunspell::generate(const std::string& word, const std::vector<std::string>& pl)
**Claim:** Same as above but the "pattern" side is supplied directly as already-split morph-description strings (bypassing the inner `analyze(pattern)` call), while `word` is still internally re-analyzed via `analyze(word)`. This lets a harness feed `pl` strings like `"is:plural"` directly (per header comment) to drive `suggest_gen`/`morphgen` without needing a pattern word that itself produces a useful analysis.
**Evidence:**
> src/hunspell/hunspell.hxx:196-203: doc comment with example `char* affix = "is:plural"; int n = generate(&result, "word", &affix, 1);`
> src/hunspell/hunspell.cxx:2386-2388: `Hunspell::generate(word, pl) { return m_Impl->generate(word, pl); }`
> src/hunspell/hunspell.cxx:1942-1946: `if (!pSMgr || pl.empty()) return slst; std::vector<std::string> pl2 = analyze(word);` — only prerequisite is non-null `pSMgr` (guaranteed by construction) and non-empty `pl`.

### Hunspell::add_dic(const char* dpath, const char* key = nullptr)
**Claim:** Configuration/source call — loads an additional `.dic` file (sharing the already-loaded `.aff` file's `affixpath`) into `m_HMgrs`, growing the set of dictionaries consulted by lookups, compound matching, etc. Must be called after the `Hunspell` object is constructed (it uses the constructor-stored `affixpath`). It can be called any number of times before or interleaved with `analyze`/`stem`/`generate`/`spell` calls — each call simply appends a `HashMgr`; there is no dictionary-removal counterpart. Return value is always `0` in current code (no error signaling to the caller beyond internal `fprintf` on hash-manager failure, see HashMgr ctor at hashmgr.cxx:92-110).
**Evidence:**
> src/hunspell/hunspell.hxx:132: `int add_dic(const char* dpath, const char* key = nullptr);` doc comment at line 131: `/* load extra dictionaries (only dic files) */`.
> src/hunspell/hunspell.cxx:2320-2322: `Hunspell::add_dic(dpath,key) { return m_Impl->add_dic(dpath, key); }`
> src/hunspell/hunspell.cxx:232-235: `HunspellImpl::add_dic(dpath,key) { m_HMgrs.push_back(std::make_unique<HashMgr>(dpath, affixpath.c_str(), key)); return 0; }` — always returns `0`; `affixpath` was captured at construction time (hunspell.cxx:196-197, member-init-list `affixpath(affpath)`).
> src/hunspell/hashmgr.cxx:92-110: `HashMgr::HashMgr` reports load errors only via `fprintf(stderr, ...)` and resizes `tableptr` to a dummy size-1 table on failure — it does not throw or set a caller-visible error flag, so `add_dic`'s return of `0` does not reflect dictionary-load success.

### How these combine in one harness (summary)
**Claim:** A single harness constructs one `Hunspell dict(aff_path, dic_path)` (required before anything else), may call `dict.add_dic(extra_dic_path)` zero or more times to broaden the loaded vocabulary, then — for one or more attacker-controlled word strings — calls `dict.analyze(word)` (the target), `dict.stem(word)` and/or `dict.stem(dict.analyze(word))`, and `dict.generate(word, word2)` / `dict.generate(word, pl)` in any order on the same `dict` object, since all of them are read-only queries against the same loaded dictionary state and do not need to be sequenced relative to each other beyond "after construction". Destruction (`delete`/scope-exit) of the single `Hunspell` object is the only required cleanup, releasing all `HashMgr`/`AffixMgr`/`SuggestMgr` resources transitively.
**Evidence:**
> src/tools/analyze.cxx:67-91: single `Hunspell* pMS` reused for `spell`, `analyze`, `generate` in a loop — the canonical real-world pattern.
> src/tools/chmorph.cxx:76-109: single `Hunspell* pMS` reused for `analyze` then `generate` per token.
> src/hunspell/hunspell.cxx:2315-2317: single destructor call releases everything.

## Build notes

**Claim:** Fuzz harnesses for this project are plain C++ translation units compiled against the static `libhunspell-1.7.a` built by the project's own `./configure && make`, with the include path set to the project's `src/` directory so `#include <hunspell/hunspell.hxx>` (or a relative `#include "../hunspell/hunspell.hxx"`) resolves to the public header shown above. Linking uses `$LIB_FUZZING_ENGINE` plus the static archive; no other external libraries are required for the `analyze`/`stem`/`generate`/`add_dic` feature (the archive bundles `HashMgr`, `AffixMgr`, `SuggestMgr`, etc.).
**Evidence:**
> oss-fuzz-build.sh:13-16: `AUTOPOINT=true autoreconf -vfi`, `./configure --disable-shared --enable-static --disable-nls`, `make -j$(nproc)` — builds `./src/hunspell/.libs/libhunspell-1.7.a`.
> oss-fuzz-build.sh:17: `$CXX $CXXFLAGS -o $OUT/fuzzer -I./src/ $LIB_FUZZING_ENGINE ./src/tools/fuzzer.cxx ./src/hunspell/.libs/libhunspell-1.7.a` — the `-I./src/` include path, C++ compiler (`$CXX`), static archive link, and `$LIB_FUZZING_ENGINE` are the pattern for every fuzzer target in this project (`morphfuzzer`, `affdicfuzzer`, `persdicfuzzer`, `parserfuzzer`, `hzfuzzer` all follow the same template at lines 24-28).
> oss-fuzz-build.sh:27: `$CXX $CXXFLAGS -o $OUT/morphfuzzer -I./src/ $LIB_FUZZING_ENGINE ./src/tools/morphfuzzer.cxx ./src/hunspell/.libs/libhunspell-1.7.a` — the build line already designated for the analyze/stem/generate feature's harness (`morphfuzzer`), though its current source (src/tools/morphfuzzer.cxx) is a stub placeholder that only keeps `Hunspell::analyze` linked via an `__asm__`-named dummy symbol (see lines 1-16 of that file) and does not actually call it — this is the file a real harness for this feature should replace.
> src/tools/fuzzer.cxx:29: `#include <hunspell/hunspell.hxx>` — confirms the public header is reached via the `-I./src/` path as `<hunspell/hunspell.hxx>` (the directory `src/hunspell/` contains `hunspell.hxx`).
> src/tools/analyze.cxx:42: `#include "../hunspell/hunspell.hxx"` — alternative relative-include style also used in the tree's tools, both resolving to the same header.
> oss-fuzz-build.sh:30-50: representative `.aff`/`.dic` test fixtures (`alias.*`, `arabic.*`, `break.*`, `circumfix.*`, `compoundflag.*`, `hu.*`, `iconv.*`, `ignore.*`, `keepcase.*`, `korean.*`, `maputf.*`, `needaffix.*`, `oconv.*`, `phone.*`, `reputf.*`, `utf8.*`, `warn.*`) are copied to `$OUT`, available at runtime for a harness (like `fuzzer.cxx`'s `LLVMFuzzerInitialize`, src/tools/fuzzer.cxx:45-73) to load multiple `Hunspell` instances covering different affix-file features (compounding, ICONV/OCONV, complex prefixes, Hungarian langnum, UTF-8) — directly relevant to the P2.10 behaviour controls of `analyze`.
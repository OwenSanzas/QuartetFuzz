## target: CMessage::Parse

### P2.1 Init Sequence
**Claim:** No library-level global init is required before calling `CMessage::Parse`. The only "init" is construction of a `CMessage` object (default or copy-constructed); `Parse` can be called directly on it, any number of times, with arbitrary `CString` input. The normal/idiomatic way to get a parsed message from raw bytes is the one-argument constructor `CMessage(const CString& sMessage)`, which calls `Parse()` then `InitTime()` — so if the harness uses that constructor it must not call `Parse()` again unless it wants to re-parse.
**Evidence:**
> include/znc/Message.h:59: `explicit CMessage(const CString& sMessage = "");`
> src/Message.cpp:22-25:
> ```
> CMessage::CMessage(const CString& sMessage) {
>     Parse(sMessage);
>     InitTime();
> }
> ```
> src/IRCSock.cpp:171: `CMessage Message(sLine);` — real caller (from `CIRCSock::ReadLine`, untrusted network data) uses the constructor, not direct `Parse()` call.
> src/Client.cpp:163: `CMessage Message(sLine);` — real caller from `CClient::ReadLine` (untrusted client data).
> test/MessageTest.cpp:42 etc.: tests construct via `CMessage("CMD p1 :p2 p3")`, never call `.Parse()` directly on an existing object in the test suite — confirming the constructor is the normal path exercising `Parse`.

### P2.2 Parameter Construction
**Claim:** `Parse` takes a single `const CString& sMessage`. `CString` is znc's `std::string`-derived type (see ZNCString.h); it can hold arbitrary bytes including embedded NUL bytes (operations are based on `.size()`, not NUL-termination) — `Parse` uses `sMessage.c_str()` and `sMessage.size()` to build the scan window, so embedded NULs are included in the scanned range but `c_str()`/pointer arithmetic is still safe since size() is used explicitly for `end`. There is no length limit enforced in `Parse` itself — any size string, including empty string, is accepted.
**Evidence:**
> src/Message.cpp:172-174:
> ```
> void CMessage::Parse(const CString& sMessage) {
>     const char* begin = sMessage.c_str();
>     const char* const end = begin + sMessage.size();
> ```
> include/znc/Message.h:164: `void Parse(const CString& sMessage);` — single by-const-ref parameter, no output parameter; state is written into the object's own member fields (`m_mssTags`, `m_Nick`, `m_sCommand`, `m_vsParams`, `m_bColon`, `m_eType`).
> No caller in the codebase pre-validates or pre-sizes `sMessage` before calling Parse/constructing CMessage (e.g. src/IRCSock.cpp:159-162 only strips `\n`/`\r`, no length check).

### P2.3 Object Lifecycle
**Claim:** Typical lifecycle is: construct `CMessage` (default or from raw line) → `Parse()` runs (directly or via the 1-arg constructor) → `InitType()` runs automatically at the end of `Parse()` → object's getters (`GetCommand`, `GetParam`, `GetTag`, `GetNick`, `GetType`, `ToString`) are used to inspect/consume the parsed data → object destroyed normally (stack object, no explicit destroy call; no heap-allocated internals requiring manual free — `MCString`/`VCString`/`CString` members self-manage). `Parse()` can be called multiple times on the same object (it `clear()`s `m_mssTags` and `m_vsParams` at the top, and unconditionally reassigns `m_sCommand`), reinitializing state for a new line.
**Evidence:**
> src/Message.cpp:187: `m_mssTags.clear();` (start of Parse — tags reset each call)
> src/Message.cpp:231: `m_vsParams.clear();` (params reset each call)
> src/Message.cpp:243: `InitType();` (always called at the end of Parse)
> src/Message.cpp:22-25: 1-arg ctor = Parse + InitTime, i.e. construct→parse→time-init in one step.
> CMessage has no custom destructor declared in Message.h (compiler-generated); no manual free needed.

### P2.4 Return Value Handling
**Claim:** `Parse` returns `void` — there is no success/failure return value and no exception is thrown for malformed input; malformed/degenerate lines (empty string, lone "@", lone ":", pure whitespace) are handled by falling through the branches with no explicit error signaling. The caller must inspect the resulting object's state afterward (e.g. `GetCommand().empty()`, `GetType() == CMessage::Type::Unknown`) to detect "garbage" input rather than checking a return code.
**Evidence:**
> include/znc/Message.h:164: `void Parse(const CString& sMessage);` — void return type.
> src/Message.cpp:172-244: entire function body has no `return` with a value, no thrown exception, and no assert; e.g. empty string: `begin==end` immediately, `*begin=='@'` check skipped because `begin<end` is false, prefix skipped, `next_word()` with `begin==end` returns an empty string_view so `m_sCommand=""`, while-loop for params doesn't execute → object ends with empty command/params and `InitType()` computes `Type::Unknown` (command length 0 ≠ 3, not PRIVMSG/NOTICE, not found in map).
> test/MessageTest.cpp:286: `EXPECT_EQ(CMessage("FOO").GetType(), CMessage::Type::Unknown);` — shows the idiom of checking `GetType()`/`GetCommand()` after parsing rather than a return value.

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup/free calls are needed for `CMessage` or for the objects `Parse` touches (`CNick m_Nick`, `MCString m_mssTags`, `VCString m_vsParams`, `CString m_sCommand` are all value members with automatic storage duration and RAII-managed internal buffers). Stack-allocated `CMessage` objects are destroyed automatically at scope exit (e.g. `CMessage Message(sLine);` in `CIRCSock::ReadLine`/`CClient::ReadLine` falls out of scope at end of the function). If a harness heap-allocates a `CMessage` (`new CMessage(...)`), it alone is responsible for `delete`.
**Evidence:**
> src/IRCSock.cpp:171: `CMessage Message(sLine);` — local/stack object, never explicitly destroyed/freed in the function (src/IRCSock.cpp:158-230ish), relies on scope exit.
> include/znc/Message.h: no `~CMessage()` declared — default destructor suffices (member-wise destruction of CNick/MCString/VCString/CString, all of which manage their own memory).

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations:
- `CMessage::Parse` — include/znc/Message.h:164 (decl), src/Message.cpp:172 (def)
- `CMessage::CMessage(const CString&)` — include/znc/Message.h:59 (decl), src/Message.cpp:22 (def)
- `CMessage::CMessage(const CNick&, const CString&, const VCString&, const MCString&)` — include/znc/Message.h:60-62 (decl), src/Message.cpp:27 (def)
- `CMessage::ToString` — include/znc/Message.h:163 (decl), src/Message.cpp:125 (def)
- `CMessage::GetParam` — include/znc/Message.h:144 (decl), src/Message.cpp:94 (def)
- `CMessage::GetParamsColon` — include/znc/Message.h:142 (decl), src/Message.cpp:53 (def)
- `CMessage::GetTag` — include/znc/Message.h:154 (decl), src/Message.cpp:113 (def)
- `CMessage::GetType` — include/znc/Message.h:98 (inline def in header)
- `CNick::Parse` — include/znc/Nick.h:39 (decl), src/Nick.cpp:41 (def)
- `CMessage::InitType` (private) — src/Message.cpp:256 (def; declared private in Message.h, not shown in public excerpt but used internally)
- `CMessage::InitTime` (private) — src/Message.cpp:246 (def)
- `CMessage::SetCommand` — include/znc/Message.h:119 (decl), src/Message.cpp:48 (def)
- `CMessage::SetParams` (both overloads) — include/znc/Message.h:134-135 (decl), src/Message.cpp:74,84 (def)
- `CMessage::SetParam` — include/znc/Message.h:145 (decl), src/Message.cpp:101 (def)
**Evidence:** see line numbers above, all confirmed by direct `Read` of include/znc/Message.h, src/Message.cpp, include/znc/Nick.h, src/Nick.cpp.

### P2.7 Co-call Constraints
**Claim:** There is no lock/unlock or begin/commit pairing for `Parse`. The one meaningful pairing is: any public setter that changes `m_sCommand` or the Text/Notice/Action/CTCP-relevant parameter (`SetCommand`, `SetParams`, `SetParam` at index 1) internally re-invokes `InitType()` to keep `GetType()` consistent — so a harness that calls `SetCommand`/`SetParams`/`SetParam` after `Parse` does not need to manually call `InitType()` again (it's private and inaccessible anyway). `CMessage(sMessage)` ctor pairs `Parse()` with `InitTime()` (not optional — always both run together in that constructor).
**Evidence:**
> src/Message.cpp:48-51: `SetCommand` → `InitType();`
> src/Message.cpp:74-82, 84-92: `SetParams` (both overloads) → conditionally call `InitType()` if `m_eType` is one of Text/Notice/Action/CTCP.
> src/Message.cpp:101-111: `SetParam` → conditionally calls `InitType()` if `uIdx==1` and current type is Text/Notice/Action/CTCP.
> src/Message.cpp:22-25: ctor always pairs `Parse()` + `InitTime()`.

### P2.8 Prerequisite State
**Claim:** `Parse` has no external prerequisite state: it does not touch `m_pNetwork`, `m_pClient`, `m_pChan` (those are set separately via `SetNetwork`/`SetClient`/`SetChan` by callers like `CIRCSock::ReadLine`/`CClient::ReadLine`, after construction, and are irrelevant to Parse's own logic). It only requires a valid `CMessage` instance (default-constructible, no prior configuration needed) and a `CString` to parse. `CNick::Parse`, invoked internally for the `:prefix`, likewise requires nothing beyond the substring it's given.
**Evidence:**
> src/IRCSock.cpp:171-172: `CMessage Message(sLine); Message.SetNetwork(m_pNetwork);` — SetNetwork happens AFTER construction/parsing, proving it's not a prerequisite for Parse.
> src/Client.cpp:163-164: `CMessage Message(sLine); Message.SetClient(this);` — same pattern, SetClient after Parse.
> include/znc/Message.h:57-59: class has a default constructor `CMessage(const CString& sMessage = "")`, usable with no arguments, before any Parse call.

### P2.9 Result Usage
**Claim:** After `Parse` fills in state (directly, or via the 1-arg constructor), real callers read it through: `GetType()` (switch dispatch to module hook functions), `GetCommand()` (string-equality checks for special low-level commands like PASS/NICK/USER before auth), `GetParam(idx)` (fetch individual, colon-stripped parameters), `GetNick()` (sender info), and `ToString()` (re-serialize, e.g. for relaying/logging). `GetTag()` is used elsewhere in the codebase (e.g. module `OnRawMessage` hooks) to read IRCv3 tags such as "time" (consumed internally by `InitTime()` too).
**Evidence:**
> src/IRCSock.cpp:171-177:
> ```
> CMessage Message(sLine);
> Message.SetNetwork(m_pNetwork);
> IRCSOCKMODULECALL(OnRawMessage(Message), &bReturn);
> if (bReturn) return;
> switch (Message.GetType()) {
>     case CMessage::Type::Account: ...
> ```
> src/Client.cpp:163,174,181,188,198:
> ```
> CMessage Message(sLine);
> ...
> CString sCommand = Message.GetCommand();
> if (sCommand.Equals("PASS")) { CString sAuthLine = Message.GetParam(0); ParsePass(sAuthLine); ... }
> else if (sCommand.Equals("NICK")) { CString sNick = Message.GetParam(0); ... }
> ```
> src/Message.cpp:246-254 (`InitTime`): `m_mssTags.find("time")` — reads a tag produced by Parse's tag-splitting loop, showing a GetTags/GetTag-style consumer right inside the Core.
> src/Buffer.cpp:22-28, 133: `CMessage Message(sFormat); ... m_Message.Parse(sFormat);` — buffered/replayed messages constructed then later used via `ToMessage`-style consumption (format strings re-parsed into CMessage for playback).
> src/Modules.cpp:802-818: `Message.Parse(sModified);` — module play-message hooks re-parse a modified raw line back into the existing CMessage object, then presumably downstream code (not shown here) continues using `Message`'s getters.

### P2.10 Behaviour Controls
**Claim:** `Parse` itself has no behavior flags — all variance comes from the *content* of `sMessage`: 
- presence of a leading `@` selects the tags block, `;`-splits tags, `=`-splits key/value, missing `=` ⇒ empty value, value is unescaped via `CString::Escape(CString::EMSGTAG, CString::EASCII)`;
- presence of a leading `:` selects prefix parsing via `CNick::Parse` (itself branches on presence of `!` and `@`);
- command token classification is driven by `InitType()`: exactly 3 ASCII digits ⇒ `Numeric`; `"PRIVMSG"` ⇒ further split by whether `GetParam(1)` starts+ends with `\001` (CTCP) and additionally starts with `"ACTION "` (Action) vs plain Text; `"NOTICE"` ⇒ CTCP vs Notice similarly; else looked up (case-insensitive via `AsUpper()`) in a static map of ~18 commands, else `Unknown`;
- presence of a leading `:` on a parameter mid-stream selects "trailing" (rest-of-line, spaces allowed) vs "middle" (space-delimited) parameter splitting, tracked in `m_bColon`.
`ToString(uFlags)` offers explicit flags `IncludeAll`/`ExcludePrefix`/`ExcludeTags` (bitmask) controlling re-serialization; `GetParamsColon`/`GetParam`/`GetParamsSplit` take numeric index/length parameters controlling which parameter(s) are returned and whether the trailing colon is re-added.
**Evidence:**
> src/Message.cpp:188-209 (tags block parsing, `;` and `=` splitting, `Escape(CString::EMSGTAG, CString::CString::EASCII)`)
> src/Message.cpp:222-224 (`:` prefix → `m_Nick.Parse(...)`)
> src/Nick.cpp:46-60 (`!` and `@` splitting inside `CNick::Parse`)
> src/Message.cpp:232-241 (`m_bColon` trailing vs middle parameter loop)
> src/Message.cpp:256-306 (`InitType`: 3-digit numeric check line 257-259; PRIVMSG branch 260-270; NOTICE branch 271-277; static map 279-298 with 18 entries; AsUpper() lookup line 299)
> include/znc/Message.h:157-163 (`FormatFlags` enum: `IncludeAll=0x0, ExcludePrefix=0x1, ExcludeTags=0x2`, used by `ToString`)
> include/znc/Message.h:133-144 (`GetParamsSplit(uIdx,uLen)`, `GetParamsColon(uIdx,uLen)`, `GetParam(uIdx)` signatures)

### P2.11 Input Expectations
**Claim:** `Parse` expects a single raw IRC protocol line as documented by the embedded BNF-style comment in the source (`<message> ::= [':' <prefix> <SPACE>] <command> <params> <crlf>`), but it does NOT itself strip or require CR/LF termination — callers are expected to strip `\r`/`\n` beforehand (seen in real callers). There's no minimum/maximum length check inside `Parse`; empty strings, strings that are only `"@"`, only `":"`, or only whitespace are all accepted without error and simply produce a `CMessage` with empty/degenerate fields (empty command ⇒ `Type::Unknown`). Separators are ASCII space `' '` only; multiple consecutive spaces are treated as one separator (consumed in the `next_word` lambda's "prepare for the following word" loop). The message is NOT required to be NUL-terminated beyond what `CString`'s `size()` dictates (the code uses `c_str()+size()` as `end`, not `strlen`), so embedded NUL bytes are included in the scanned content (a `CString` holding embedded NULs will have those bytes treated as ordinary non-space octets).
**Evidence:**
> src/Message.cpp:211-219 (BNF comment: `<message> ::= [':' <prefix> <SPACE>] <command> <params> <crlf>` etc. — note `<crlf>` is part of the grammar description but Parse's code never looks for `\r`/`\n`).
> src/IRCSock.cpp:160-162: `sLine.Replace("\n", ""); sLine.Replace("\r", "");` — caller-side CRLF stripping before constructing `CMessage Message(sLine);` at line 171, confirming Parse itself does not handle CRLF.
> src/Message.cpp:173-174: `const char* begin = sMessage.c_str(); const char* const end = begin + sMessage.size();` — uses `.size()`, not NUL-termination, as the scan boundary.
> src/Message.cpp:178: `while (p < end && *p != ' ') ++p;` — only ASCII space is a word separator.
> src/Message.cpp:182: `while (begin < end && *begin == ' ') ++begin;` — collapses runs of multiple spaces.
> test/MessageTest.cpp:42-48 (e.g. `CMessage("CMD").GetParams(0)` etc.) — shows minimal/degenerate "CMD" with no params handled without error.

### Build notes
**Claim:** The OSS-Fuzz harness for this project (`msg_parse_fuzzer.cpp`) is C++ (the function `Parse` is a C++ class method; the existing baseline harness links to it via its C++ mangled symbol `_ZN8CMessage5ParseERK7CString`). It's compiled with `-std=c++11`, with the target's header reachable at `-I/src/znc/include` (public header path for `znc/Message.h`) and `-I/src/znc/build/include` (generated `zncconfig.h`/build config), and `-include znc/zncconfig.h` is force-included before any znc header. The library is pre-built via CMake (`-DBUILD_SHARED_LIBS=OFF -DWANT_ICU=OFF -DWANT_OPENSSL=OFF -DWANT_ZLIB=OFF -DWANT_IPV6=OFF`) producing `/src/znc/build/src/libznc.a`, which the fuzzer object is statically linked against together with `$LIB_FUZZING_ENGINE`.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/znc-c1/build.sh:27-33:
> ```
> $CXX $CXXFLAGS -DGTEST_HAS_POSIX_RE=0 \
>     -I/src/znc/include -I/src/znc/build/include \
>     -fPIE -include znc/zncconfig.h -std=c++11 \
>     -c $SRC/msg_parse_fuzzer.cpp -o msg_parse_fuzzer.o
> $CXX $CXXFLAGS $LIB_FUZZING_ENGINE \
>     msg_parse_fuzzer.o -o $OUT/msg_parse_fuzzer \
>     /src/znc/build/src/libznc.a
> ```
> ${QF_WORK}/ossfuzz_view/znc-c1/Dockerfile:17-21: base image `gcr.io/oss-fuzz-base/base-builder`, clones `znc` repo, copies `build.sh` and `msg_parse_fuzzer.cpp` into `$SRC`.
> ${QF_WORK}/ossfuzz_view/znc-c1/msg_parse_fuzzer.cpp:10: `QF_EXTERN void qf_target_0(void) __asm__("_ZN8CMessage5ParseERK7CString");` — confirms target symbol is a C++ mangled method on `CMessage`, consistent with `include/znc/Message.h:164`'s declaration `void Parse(const CString& sMessage);` inside `class CMessage`. (Current file is only a placeholder/stub that doesn't actually call Parse — `LLVMFuzzerTestOneInput` body ignores `data`/`size` — the real harness to be generated must actually feed fuzz bytes into `CMessage`/`Parse`.)
> include/znc/Message.h:26-28: `#include <znc/zncconfig.h>`, `#include <znc/ZNCString.h>`, `#include <znc/Nick.h>` — header dependencies that must be resolvable from the include paths above when a harness does `#include <znc/Message.h>`.

## Other entries (Logic Group) — call protocol & combination with target

### CMessage::CMessage(const CString& sMessage = "") — constructor entry
**Claim:** Public, explicit, 1-arg constructor (default argument `""` also makes it usable as default ctor). Internally calls `Parse(sMessage)` then `InitTime()` — this IS the normal way the target is invoked on untrusted bytes in the real codebase (`CIRCSock::ReadLine`, `CClient::ReadLine`). Returns a fully-initialized `CMessage` object (tags/prefix/command/params/type/time all set). No separate call to `Parse()` is needed (and should not be duplicated) if this constructor is used. No explicit destroy needed (stack lifetime / default dtor).
**Evidence:**
> include/znc/Message.h:59: `explicit CMessage(const CString& sMessage = "");`
> src/Message.cpp:22-25: `CMessage::CMessage(const CString& sMessage) { Parse(sMessage); InitTime(); }`
**Combination with target:** The harness can call this constructor directly with fuzz bytes wrapped in a `CString` — this both constructs the object and exercises `Parse` (and transitively `InitType`, `CNick::Parse`, `InitTime`). This is preferred over calling the default ctor + `.Parse()` separately, since it's what real callers (IRCSock/Client) do, though calling `.Parse()` explicitly on a default-constructed object is equally valid per P2.1/P2.3 and gives identical results except `InitTime()` is skipped (so `GetTime()` keeps whatever the default-constructed `m_time` is, not the tag/clock-derived one — check `CMessage()` default ctor / member init for `m_time`'s default, not located in the excerpts read, likely zero-initialized `timeval`).

### CMessage::ToString(unsigned int uFlags = IncludeAll) const — consumer entry
**Claim:** Must be called on an already-`Parse`d (or otherwise populated, e.g. via the 4-arg ctor) `CMessage`. Pure function, `const`, no mutation, no allocation requiring cleanup beyond the returned `CString` by value. `uFlags` is a bitmask of `FormatFlags`: `IncludeAll=0x0` (default — include tags+prefix), `ExcludePrefix=0x1`, `ExcludeTags=0x2` (combinable via `|`). Re-serializes tags (re-escaping values via `Escape_n(CString::EMSGTAG)`), prefix (via `m_Nick.GetHostMask()`), command, and params (via `GetParamsColon(0)`, which re-adds the trailing `:` per the `m_bColon`/empty/leading-colon/contains-space rule).
**Evidence:**
> include/znc/Message.h:157-163: `enum FormatFlags { IncludeAll=0x0, ExcludePrefix=0x1, ExcludeTags=0x2 }; CString ToString(unsigned int uFlags = IncludeAll) const;`
> src/Message.cpp:125-170: full body, e.g. line 137 `sTags += "=" + it.second.Escape_n(CString::EMSGTAG);`, line 166 `sMessage += GetParamsColon(0);`
> test/MessageTest.cpp:146-179, 247-250: real usage, e.g. `CMessage("CMD p1 :p2 p3").ToString()`-style round trips and `msg.ToString(CMessage::ExcludeTags)`.
**Combination with target:** Natural round-trip check in a harness: `CMessage m(sFuzzInput); CString s = m.ToString();` (optionally re-parse `s` into a second `CMessage` and compare) — exercises both decode (target) and encode paths as the task description suggests. No prerequisite beyond having called/invoked `Parse` (directly or via ctor) first.

### CMessage::GetParam(unsigned int uIdx) const — consumer entry
**Claim:** Must be called after parsing/populating params. Bounds-checked internally: returns `""` for `uIdx >= m_vsParams.size()`, so it is safe to call with any `uIdx` including out-of-range/huge values (e.g. values derived from fuzz bytes) — no crash, exercises the bounds check itself. Returns a `CString` by value (no cleanup needed).
**Evidence:**
> src/Message.cpp:94-99: `CString CMessage::GetParam(unsigned int uIdx) const { if (uIdx >= m_vsParams.size()) return ""; return m_vsParams[uIdx]; }`
> src/Client.cpp:181,188,198: `CString sAuthLine = Message.GetParam(0);`, `CString sNick = Message.GetParam(0);` — real usage right after `CMessage Message(sLine);`.
**Combination with target:** Harness can call `m.GetParam(i)` for several `i` (including values taken from the fuzz input or boundary values like `0`, `m.GetParams().size()`, `UINT_MAX`) right after `Parse`/ctor, to exercise bounds-checking per the feature description.

### CMessage::GetParamsColon(unsigned int uIdx, unsigned int uLen = -1) const — consumer entry
**Claim:** Must be called after parsing. Also internally bounds-checked: returns `""` when `m_vsParams.empty() || uLen==0 || uIdx>=m_vsParams.size()`; clamps `uLen` if it overruns the vector. Reconstructs a space-joined string from params `[uIdx, uIdx+uLen)`, re-adding a leading `:` to the last included parameter if `m_bColon` was set during parsing, or if that parameter is empty/starts with `:`/contains a space. Used both by consumers directly and internally by `ToString()`.
**Evidence:**
> src/Message.cpp:53-72: full body (bounds check lines 54-59, loop+colon-logic lines 60-70).
> src/Message.cpp:166: `ToString()` calls `GetParamsColon(0)` internally.
> test/MessageTest.cpp:75-...: extensive `GetParamsColon` boundary tests, e.g. line 76-86 with out-of-range/zero-length indices on a bare `"CMD"` message.
**Combination with target:** Call right after `Parse`/ctor with varying `uIdx`/`uLen` (including `0`, large values, `(unsigned)-1` as used in tests/declared default) to mirror `GetParams(uIdx,uLen)`/`GetParamsColon` test coverage and the deprecated `GetParams` wrapper (Message.h:138-141) which forwards to it.

### CMessage::GetTag(const CString& sKey) const — consumer entry
**Claim:** Must be called after tags have been parsed (i.e., after `Parse`/ctor, or after `SetTag`/`SetTags`). Simple map lookup on `m_mssTags`; returns `""` if key not found — no exception, no bounds issue. `sKey` can be any string including empty or ones never present in the tags block.
**Evidence:**
> src/Message.cpp:113-119: `CString CMessage::GetTag(const CString& sKey) const { ... if (it != m_mssTags.end()) return it->second; return ""; }`
> src/Message.cpp:247-248 (`InitTime`): `auto it = m_mssTags.find("time"); if (it != m_mssTags.end()) { m_time = CUtils::ParseServerTime(it->second); ... }` — shows the same map being read via a `GetTag`-equivalent pattern internally right after Parse populates `m_mssTags`.
**Combination with target:** Call `m.GetTag("time")`, `m.GetTag("<fuzz-derived key>")` etc. right after `Parse`/ctor to exercise the tag-lookup path for both tags present (from `@key=value ...` inputs) and absent.

### CMessage::GetType() const — consumer entry
**Claim:** Inline getter, just returns the `m_eType` enum field set by `InitType()` (called at the end of `Parse`, and re-called by `SetCommand`/`SetParams`/`SetParam` as covered in P2.7). No prerequisite beyond `Parse` (or an equivalent setter path) having run at least once. No cleanup. Return is a plain enum value by value.
**Evidence:**
> include/znc/Message.h:98: `Type GetType() const { return m_eType; }`
> src/IRCSock.cpp:177: `switch (Message.GetType()) { case CMessage::Type::Account: ... }` — real dispatch usage immediately after construction/parsing (line 171) and an `OnRawMessage` module hook (line 174).
**Combination with target:** After `Parse`/ctor, call `GetType()` and (optionally) switch on all/most of the 23 `Type` enum values to mirror `CIRCSock::ReadLine`'s dispatch, giving coverage-relevant branching keyed off the fuzzed command string (per P2.10).

### CNick::Parse (Core, not a public "entry" of this Logic Group's API surface but invoked by the target)
**Claim:** `CNick::Parse` is a *Core* internal helper (per the Logic Group classification), not one of the 7 public Entries, but it is directly invoked by `CMessage::Parse` whenever the raw line begins with `:` (the `<prefix>` case). It belongs to `CNick`, a separate public class (`include/znc/Nick.h`), and is reachable standalone too (e.g. via `CNick(const CString&)` ctor, src/Nick.cpp:32). It expects a nick-mask substring (already stripped of the leading `:` is NOT required — note `CMessage::Parse` passes `next_word().substr(1)` i.e. the `:` IS stripped before calling `CNick::Parse`, src/Message.cpp:223), and splits on `!` then `@`. No init/cleanup needed (same lightweight value-type pattern as CMessage).
**Evidence:**
> src/Message.cpp:222-224: `if (begin < end && *begin == ':') { m_Nick.Parse(std::string(next_word().substr(1))); }` — `:` already stripped via `.substr(1)` before being handed to `CNick::Parse`.
> src/Nick.cpp:41-61: `CNick::Parse` body — handles an optional leading `:` defensively too (`sNickMask[0]==':'` checks at lines 49 and 54), splits on `!` (line 46) then `@` (line 57).
**Combination with target:** Exercised automatically whenever the harness feeds a line starting with `:` into `CMessage::Parse`/ctor; the harness does not need to call `CNick::Parse` separately, but can additionally inspect `m.GetNick().GetNick()/GetIdent()/GetHost()` afterward (CNick getters, include/znc/Nick.h:63-65) to validate the prefix split, as the feature description notes.

### Summary: how entries combine in one harness
A single harness exercising this Logic Group should, per real-callers' idiom (src/IRCSock.cpp:171, src/Client.cpp:163):
1. Build a `CString` from the fuzz bytes (no length/content pre-validation needed — matches P2.1/P2.11).
2. Construct `CMessage m(sFuzzInput);` — this calls the target `Parse()` (exercising tags/prefix/command/params splitting and `InitType`/`CNick::Parse`) plus `InitTime()`.
3. Call the consumer entries on `m`: `m.GetType()`, `m.GetCommand()`, `m.GetParam(i)` for a few `i` (including out-of-range), `m.GetParamsColon(i,n)`, `m.GetTag(k)` for a few keys (e.g. `"time"` and a fuzz-derived key), `m.GetNick()` accessors.
4. Call `m.ToString()` (with each of `IncludeAll`/`ExcludePrefix`/`ExcludeTags`/combo) to exercise the encode path and optionally re-parse the result into a second `CMessage` for a round-trip comparison (`Equals`, include/znc/Message.h:100, src/Message.cpp:36-40).
5. No explicit cleanup needed; let `CMessage` objects go out of scope (matches P2.5).
No ordering constraints beyond "Parse (or the 1-arg ctor) must run before any getter is meaningful" — all getters are safe (bounds-checked) even on a default/empty/never-parsed object, so there is no crash risk from calling them "too early", only semantically-empty results.
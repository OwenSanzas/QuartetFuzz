## target: wxZipInputStream::GetNextEntry

### Overview
`wxZipEntry *GetNextEntry();` is declared in `include/wx/zipstrm.h:394` and defined in `src/common/zipstrm.cpp:1809-1822`. It overrides the abstract `DoGetNextEntry()` from `wxArchiveInputStream` (`include/wx/archive.h:100,108`: `GetNextEntry() { return DoGetNextEntry(); }`), and `zipstrm.h:413`: `wxArchiveEntry *DoGetNextEntry() override { return GetNextEntry(); }`.

```
// src/common/zipstrm.cpp:1809
wxZipEntry *wxZipInputStream::GetNextEntry()
{
    if (m_position == wxInvalidOffset)
        if (!LoadEndRecord())
            return nullptr;

    m_lasterror = m_parentSeekable ? ReadCentral() : ReadLocal();
    if (!IsOk())
        return nullptr;

    std::unique_ptr<wxZipEntry> entry(new wxZipEntry(m_entry));
    entry->m_backlink = m_weaklinks->AddEntry(entry.get(), entry->GetKey());
    return entry.release();
}
```

It returns a new, heap-allocated `wxZipEntry*` that the **caller owns and must delete**; returns `nullptr` on EOF or error (see archive.h:88 "When there are no more entries, GetNextEntry() returns nullptr and sets Eof()").

### P2.1 Init Sequence
**Claim:** Construct a `wxZipInputStream` from an already-open `wxInputStream` (e.g. `wxFileInputStream`/`wxMemoryInputStream`); no other library-wide init is required before the first `GetNextEntry()` call. The constructor just calls `Init()`.
**Evidence:**
> include/wx/zipstrm.h:386-387: `wxZipInputStream(wxInputStream& stream, wxMBConv& conv = wxConvLocal); wxZipInputStream(wxInputStream *stream, wxMBConv& conv = wxConvLocal);`
> src/common/zipstrm.cpp:1605-1621 `wxZipInputStream::Init()`: sets `m_store = new wxStoredInputStream(...)`, `m_position = wxInvalidOffset`, `m_weaklinks = new wxZipWeakLinks`, etc. — no file/archive parsing happens yet.
> tests/archive/ziptest.cpp:187-189: `wxZipInputStream zip(in); std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry());` — direct construct-then-call pattern.
> tests/fuzz/tar.cpp:41-43 (sibling tar fuzzer, same pattern as zip would use): `wxMemoryInputStream mis(data, size); wxTarInputStream tis(mis); while ( wxTarEntry* const te = tis.GetNextEntry() ) {...}`

On the very first call, `GetNextEntry()` itself lazily calls `LoadEndRecord()` (src/common/zipstrm.cpp:1811-1813) which in turn calls `FindEndRecord()` (searches backward for EOCD on seekable streams) — so no explicit "open archive" call is needed beyond constructing the stream.

### P2.2 Parameter Construction
**Claim:** `GetNextEntry()` takes no parameters. The implicit parameter is `this` (the stream object), which must wrap a valid `wxInputStream&` / `wxInputStream*` supplied at construction, and `conv` (default `wxConvLocal`) selecting the multi-byte encoding used to decode entry names/comments unless the `wxZIP_LANG_ENC_UTF8` flag bit forces UTF-8 (src/common/zipstrm.cpp:1100-1102, 1185-1187).
**Evidence:**
> include/wx/zipstrm.h:394: `wxZipEntry *GetNextEntry();` — no parameters.
> src/common/zipstrm.cpp:1100-1102: `wxMBConv& strConv = m_Flags & wxZIP_LANG_ENC_UTF8 ? static_cast<wxMBConv&>(wxConvUTF8) : conv;`

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: construct `wxZipInputStream` over an input stream → loop calling `GetNextEntry()` (each call auto-closes the previous entry via `ReadCentral()/ReadLocal()` → `CloseEntry()` if not at header) → optionally `OpenEntry()`/read data for each returned entry → destroy the returned `wxZipEntry*` (caller-owned) → destroy the `wxZipInputStream` (destructor cleans up decompressor/internal streams).
**Evidence:**
> src/common/zipstrm.cpp:1826-1827 (`ReadCentral`): `if (!AtHeader()) CloseEntry();` — each subsequent `GetNextEntry()` call closes the still-open previous entry's data stream first.
> src/common/zipstrm.cpp:1870-1871 (`ReadLocal`): same `if (!AtHeader()) CloseEntry();` pattern for non-seekable streams.
> src/common/zipstrm.cpp:1623-1635 `wxZipInputStream::~wxZipInputStream()`: `CloseDecompressor(m_decomp); delete m_store; delete m_inflate; delete m_rawin; m_weaklinks->Release(this); if (m_streamlink) m_streamlink->Release(this);`
> tests/archive/archivetest.cpp:731 / 1164-1172: entries are wrapped in smart pointers (`EntryPtr`, `std::unique_ptr<wxArchiveEntry>`) immediately after `GetNextEntry()` returns, confirming caller ownership.

### P2.4 Return Value Handling
**Claim:** Return `nullptr` means either legitimate end-of-archive (`Eof()` becomes true) or a parse error (`IsOk()` false, `GetLastError()` != `wxSTREAM_NO_ERROR`/`wxSTREAM_EOF`). Callers must check the returned pointer, not assume non-null, and loop `while (entry = stream.GetNextEntry())`.
**Evidence:**
> include/wx/archive.h:88: "When there are no more entries, GetNextEntry() returns nullptr and sets Eof()."
> src/common/zipstrm.cpp:1815-1817: `m_lasterror = m_parentSeekable ? ReadCentral() : ReadLocal(); if (!IsOk()) return nullptr;`
> src/common/zipstrm.cpp:1829-1830 (`ReadCentral`): `if (m_signature == END_MAGIC || m_signature == Z64_END_MAGIC) return wxSTREAM_EOF;` — clean end.
> src/common/zipstrm.cpp:1832-1835: malformed signature → `wxLogError(...); return wxSTREAM_READ_ERROR;` — error end.
> samples/archive/archive.cpp:264-266: `for (wxArchiveEntry* entry = archiveStream->GetNextEntry(); entry; entry = archiveStream->GetNextEntry())`.
> src/common/fs_arc.cpp:146: `while ((entry = m_archive->GetNextEntry()) != nullptr) { AddToCache(entry); ... }`.

### P2.5 Cleanup Sequence
**Claim:** On success: `delete entry;` (or wrap in smart pointer) once done reading its data; the owning `wxZipInputStream` is deleted/destructed after the loop ends, which releases `m_store`/`m_inflate`/`m_rawin`/`m_weaklinks`/`m_streamlink`. On error: same — no special extra cleanup is required, `~wxZipInputStream` handles partial state because `CloseDecompressor(m_decomp)` tolerates `m_decomp == nullptr`.
**Evidence:**
> src/common/zipstrm.cpp:1623-1635 (destructor, shown above).
> src/common/fs_arc.cpp:128-132 `CloseStreams()`: `wxDELETE(m_archive); wxDELETE(m_stream);` — caller deletes the whole `wxZipInputStream`-derived archive object after the entry loop completes (src/common/fs_arc.cpp:146-156).
> samples/archive/archive.cpp:264-277, 297-305: entries obtained from `GetNextEntry()` are not even stored past iteration (leaked in the sample, but tests use unique_ptr — see archivetest.cpp:731).

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at `src/common/zipstrm.cpp` / `include/wx/zipstrm.h` / `include/wx/archive.h`. Confirmed via direct `Read`/`Grep` of the project tree (paths and line numbers cited throughout this report). No fabricated names.

### P2.7 Co-call Constraints
**Claim:** `GetNextEntry()` pairs with implicit `CloseEntry()` (automatically invoked at the start of the next `ReadCentral`/`ReadLocal` call, or explicitly callable) — there is no explicit "begin/end entry" pair the caller must issue; reading the next entry via `GetNextEntry()` implicitly closes the prior one. `OpenEntry(entry)` is the "open" half when re-visiting a previously-returned entry (seekable-only); it has no separate explicit "close" call other than the implicit one described above or an explicit `CloseEntry()`.
**Evidence:**
> src/common/zipstrm.cpp:1826-1827, 1870-1871 (shown above: auto `CloseEntry()` call).
> include/wx/zipstrm.h:392: `bool WXZIPFIX CloseEntry() override;` — public, explicitly callable.
> include/wx/zipstrm.h:391: `bool OpenEntry(wxZipEntry& entry) { return DoOpen(&entry); }`.
> tests/archive/ziptest.cpp:117-120: "for seekable streams, GetNextEntry() doesn't read the local header so call OpenEntry() to do it" — `if (arc && (m_options & PipeIn) == 0 && entry.IsDir()) arc->OpenEntry(entry);`

### P2.8 Prerequisite State
**Claim:** The only prerequisite state is a valid, constructed `wxInputStream` passed to the `wxZipInputStream` constructor (it can be any `wxInputStream`: file, memory, pipe/non-seekable). Internally `IsSeekable()` on that parent stream determines whether `ReadCentral()` (seekable, central-directory-driven) or `ReadLocal()` (sequential, local-header-driven) path is taken — this is auto-detected, not caller-configured.
**Evidence:**
> src/common/zipstrm.cpp:1815: `m_lasterror = m_parentSeekable ? ReadCentral() : ReadLocal();`
> src/common/zipstrm.cpp:1754-1764 (`FindEndRecord`): `if (!m_parent_i_stream->IsSeekable()) return false; ... m_parentSeekable = true;` — seekability is probed, not asserted by caller.
> src/common/zipstrm.cpp:1605-1621 `Init()` — state fully initialized by constructor, nothing else needed from caller.

### P2.9 Result Usage
**Claim:** The returned `wxZipEntry*` is consumed in several ways by real callers:
1. Query metadata: `GetName()`, `GetSize()`, `GetDateTime()`, `GetComment()`, `IsDir()`, `GetKey()`/`GetOffset()`.
2. Passed to `wxZipInputStream::OpenEntry(wxZipEntry&)` to (re)position/parse the local header for that specific entry (seekable streams only).
3. Used as the key while reading the entry's *decompressed* payload via `wxInputStream::Read()`/`wxZipInputStream::OnSysRead()` on the **same** `wxZipInputStream` object (not on the entry) until `Eof()`.
4. Passed to `wxZipOutputStream::CopyEntry(entry, inputStream)` to copy an entry across archives (declared `friend` at zipstrm.h:450-451).
5. Cached in a hash map keyed by name (`wxArchiveFSCacheDataImpl::AddToCache`), used for `wxFileSystem` zip-backed browsing.
**Evidence:**
> tests/fuzz/tar.cpp:43-52 (companion tar fuzzer, identical GetNextEntry/OpenEntry/Read/CloseEntry API shape as zip): `while (wxTarEntry* const te = tis.GetNextEntry()) { if (tis.OpenEntry(*te)) { ...tis.Read(buf, sizeof(buf))...; tis.CloseEntry(); } delete te; }`
> tests/archive/ziptest.cpp:189-203: `std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry()); ... zip.Read(buf, sizeof(buf)-1).LastRead(); ... zip.Eof()` — reading goes through the `wxZipInputStream`, using entry only for size check.
> tests/archive/archivetest.cpp:756-775: `arc->Read(buf.data(), testEntry.GetSize()+1).Eof(); arc->LastRead(); arc->GetLength() == entry->GetSize()` — entry metadata (`GetSize`) is compared against stream behavior (`arc->GetLength()`, `arc->Read`).
> src/common/fs_arc.cpp:117-126, 134-157 `AddToCache`/`Get`: `m_hash[entry->GetName(wxPATH_UNIX)] = ...; if (entry->GetName(wxPATH_UNIX) == name) return entry;`
> samples/archive/archive.cpp:264-277: `entry->GetSize()`, `entry->GetDateTime()`, `entry->GetName()` used for listing; 297-304 `entry->GetName()`, `entry->GetSize()` used in `CopyStreamData(*archiveStream, outputFileStream, entry->GetSize())` to extract data from the **stream**, not the entry.
> zipstrm.h:450-453 friend declarations for `wxZipOutputStream::CopyEntry` / `CopyArchiveMetaData`, consuming `wxZipInputStream&`.

### P2.10 Behaviour Controls
**Claim:** Behaviour of `GetNextEntry()` and its surrounding API is controlled by:
- **Parent stream seekability** → selects `ReadCentral()` (central-directory walk) vs `ReadLocal()` (sequential local-header walk) (zipstrm.cpp:1815, 1754-1764).
- **Entry's compression method** (`wxZIP_METHOD_STORE` vs `wxZIP_METHOD_DEFLATE`, others rejected) → selects decompressor in `OpenDecompressor()` (zipstrm.cpp:2047-2070): `switch (m_entry.GetMethod()) { case wxZIP_METHOD_STORE: ...; case wxZIP_METHOD_DEFLATE: ...; default: wxLogError(_("unsupported Zip compression method")); }`
- **`wxZIP_SUMS_FOLLOW` flag bit** (in `m_Flags`, read in `ReadLocal`) → when set, sizes/CRC in local header are not trusted (`sumsValid = (m_Flags & wxZIP_SUMS_FOLLOW) == 0;` zipstrm.cpp:1089) and a trailing data descriptor is parsed after the payload via `ReadDescriptor()` (zipstrm.cpp:2130-2131: `if ((m_entry.GetFlags() & wxZIP_SUMS_FOLLOW) != 0) m_headerSize += m_entry.ReadDescriptor(...)`).
- **`raw` parameter to `DoOpen(entry, raw)`/`OpenDecompressor(raw)`** → raw mode returns compressed bytes unprocessed (store-passthrough or `wxRawInputStream`) instead of inflating (zipstrm.cpp:2011-2038, 2017-2028).
- **ZIP64 sentinel `0xffffffff`** in size/compressed-size/offset fields of local/central headers → triggers `LoadExtraInfo()` to read 64-bit overrides from the "extra" field with Header ID=1 (zipstrm.cpp:1038-1059).
- **`readEndRec` parameter to `ReadLocal(bool readEndRec=false)`** → used by `GetComment()` to force reading the EOCD comment on non-seekable streams (zipstrm.cpp:1643-1645).
**Evidence:** line numbers above, all within `src/common/zipstrm.cpp`.

### P2.11 Input Expectations
**Claim:** The target expects a well-formed PKZIP byte stream: 4-byte little-endian magic signatures (`LOCAL_MAGIC` 'PK\x03\x04', `CENTRAL_MAGIC`, `END_MAGIC`, `Z64_END_MAGIC`), fixed-size header structs (`LOCAL_SIZE`, `CENTRAL_SIZE`, `SUMS_SIZE`, `END_SIZE`) followed by variable-length name/extra/comment fields whose lengths are declared as `wxUint16` in the header itself. There is no overall maximum input size enforced beyond the normal `wxUint16`/`wxUint32` field ranges and the stream's own EOF; length checks against actual bytes read are done (`stream.LastRead() != nameLen + 0u` etc.) and return 0 (parse failure) on short reads. The extra-field parser (`LoadExtraInfo`) explicitly guards against malformed/short ZIP64 records (zipstrm.cpp:1020-1049) to avoid reading past the buffer/using uninitialized stack bytes — added as a fix for a real bug (see `Zip::BadZip64ExtraField` test, ziptest.cpp:254-275).
**Evidence:**
> src/common/zipstrm.cpp:1081-1083 (`ReadLocal`): `wxZipHeader ds(stream, LOCAL_SIZE - 4); if (!ds) return 0;`
> src/common/zipstrm.cpp:1104-1105, 1111-1112, 1189-1190, 1196-1197, 1209-1210: repeated `if (stream.LastRead() != xxxLen + 0u) return 0;` guards after reading variable-length fields.
> src/common/zipstrm.cpp:1012-1070 (`LoadExtraInfo`): iterates extra-field records with explicit bounds checks `if (extraData + 4 + fieldLen > extraDataEnd) { wxLogWarning(...); return false; }` and ZIP64-length checks `if (fieldLen < z64Required) {...return false;}`.
> tests/archive/ziptest.cpp:254-275 `TEST_CASE("Zip::BadZip64ExtraField", ...)`: constructs a hand-crafted malformed ZIP64 local header to regression-test the above guard.

## Other Entries of this Logic Group (API surface, how they combine with GetNextEntry)

### wxZipInputStream::OpenEntry
**Declaration:** `bool OpenEntry(wxZipEntry& entry) { return DoOpen(&entry); }` (include/wx/zipstrm.h:391, inline); also the polymorphic override `bool OpenEntry(wxArchiveEntry& entry) override;` (zipstrm.h:415, defined zipstrm.cpp:1948-1952) which does `wxZipEntry *zipEntry = wxStaticCast(&entry, wxZipEntry); return zipEntry ? OpenEntry(*zipEntry) : false;`
**Needs before it:** A `wxZipEntry` previously obtained from `GetNextEntry()` on the *same* `wxZipInputStream` object (or a seekable stream's entry retained across reopening). Internally calls `DoOpen(entry, raw=false)` (zipstrm.cpp:1956-2009).
**Parameters / path selection:**
- `entry->GetKey() == m_entry.GetOffset()` and `AfterHeader()` ⇒ fast-path, header already parsed, returns `true` immediately (zipstrm.cpp:1969-1970).
- Otherwise: `wxCHECK(m_parentSeekable, false);` — **opening an arbitrary stored entry by reference requires a seekable parent stream**; on non-seekable streams only the "current" entry can be opened (zipstrm.cpp:1971-1972).
- If seekable: seeks to `m_entry.GetOffset()`, verifies `ReadSignature() == LOCAL_MAGIC` else logs `"bad zipfile offset to entry"` and fails (zipstrm.cpp:1980-1988).
- Calls `m_entry.ReadLocal(...)` to re-parse the local header at that offset (zipstrm.cpp:1991), i.e. this is the "ReadLocal header re-parsing" path mentioned in the feature description.
**Return value:** `bool` — `true`/`IsOk()` on success, `false` on seek/signature/parse failure.
**What to free:** Nothing extra — operates on the entry/stream already owned by caller; no new allocation returned.
**Combination with target:** `wxZipEntry* e = zip.GetNextEntry(); if (e) { zip.OpenEntry(*e); /* then zip.Read(...) */ }` — but note `OpenEntry` is largely redundant immediately after `GetNextEntry()` on a seekable stream since `OnSysRead`/first `Read()` call auto-opens via `DoOpen()` with `entry=nullptr` (zipstrm.cpp:2115-2119: `if (!IsOpened()) if ((AtHeader() && !DoOpen()) || !OpenDecompressor()) ...`). Tests call `OpenEntry` explicitly mainly for directory entries or to force local-header parsing (ziptest.cpp:117-120).

### wxZipInputStream::GetComment
**Declaration:** `wxString WXZIPFIX GetComment();` (zipstrm.h:396), defined zipstrm.cpp:1637-1649.
**Needs before it:** Nothing explicit — it lazily calls `LoadEndRecord()` itself if `m_position == wxInvalidOffset` (zipstrm.cpp:1639-1641). For **non-seekable** streams it must additionally force full parsing to the end: `if (!m_parentSeekable && Eof() && m_signature) { m_lasterror = wxSTREAM_NO_ERROR; m_lasterror = ReadLocal(true); }` (zipstrm.cpp:1643-1646) — this only works correctly if the caller has already consumed all entries (reached `Eof()`) via repeated `GetNextEntry()`/`Read()` calls; calling `GetComment()` too early on a non-seekable stream may return an empty/incomplete comment.
**Parameters:** none.
**Return value:** `wxString` (archive comment); empty string if `LoadEndRecord()` fails.
**What to free:** Nothing (`wxString` is value type).
**Combination with target:** Typically called *after* fully draining `GetNextEntry()` loop on a non-seekable stream, or any time on a seekable stream (since `LoadEndRecord()` eagerly parses EOCD). Test evidence: `tests/archive/ziptest.cpp:70`: `CPPUNIT_ASSERT(zip.GetComment() == m_comment);` called from `OnArchiveExtracted` which runs for seekable archives before/alongside the entry loop.

### wxZipInputStream::GetTotalEntries
**Declaration:** `int WXZIPFIX GetTotalEntries();` (zipstrm.h:397), defined zipstrm.cpp:1651-1656: `if (m_position == wxInvalidOffset) LoadEndRecord(); return m_TotalEntries;`
**Needs before it:** Nothing explicit (self-triggers `LoadEndRecord()`), but the returned count comes from the EOCD record (`m_TotalEntries = endrec.GetTotalEntries();` zipstrm.cpp:1713) and is **attacker-controlled** — it is not necessarily validated against the actual number of entries `GetNextEntry()` will yield (sequential/non-seekable path increments `m_TotalEntries++` per local header instead, zipstrm.cpp:1931-1933).
**Parameters:** none. **Return value:** `int`, the declared count (0 if `LoadEndRecord()` fails and stream is non-seekable-with-no-data, or the EOCD's `TotalEntries` field).
**What to free:** nothing.
**Combination with target:** Called before or interleaved with the `GetNextEntry()` loop purely to query a count (e.g., for a progress bar or sanity check); does not gate the loop. Test evidence: `tests/archive/ziptest.cpp:71`: `CPPUNIT_ASSERT(zip.GetTotalEntries() == expectedTotal);`

### wxZipInputStream::OnSysRead (protected; invoked indirectly through `wxInputStream::Read()`)
**Declaration:** `size_t WXZIPFIX OnSysRead(void *buffer, size_t size) override;` (zipstrm.h:402), defined zipstrm.cpp:2115-2137. Not called directly by harness code — call `zip.Read(buffer, size)` (public `wxInputStream` API) on the stream itself, which dispatches to `OnSysRead`.
**Needs before it:** A `GetNextEntry()` call must have succeeded (so `m_entry`/header state is populated); `OnSysRead` auto-opens via `DoOpen()`/`OpenDecompressor()` if not already opened (zipstrm.cpp:2117-2119).
**Parameters / path selection:** Decompression path chosen by `m_entry.GetMethod()` inside `OpenDecompressor()` (STORE vs DEFLATE, see P2.10). CRC is accumulated unless `m_raw` is set (zipstrm.cpp:2124-2125: `if (!m_raw) m_crcAccumulator = crc32(...)`). On reaching `Eof()`, if `wxZIP_SUMS_FOLLOW` flag set, reads the trailing data descriptor via `m_entry.ReadDescriptor(*m_parent_i_stream)` (zipstrm.cpp:2129-2131) and reconciles against `m_weaklinks`-tracked entry (continues past the shown snippet).
**Return value:** `size_t` bytes actually read (may be less than requested at EOF or on stream error, check `GetLastError()`).
**What to free:** Nothing new; `buffer` is caller-owned.
**Combination with target:** `wxZipEntry* e = zip.GetNextEntry(); while (e) { char buf[N]; while (zip.IsOk() && !zip.Eof()) { zip.Read(buf, N); if (!zip.LastRead()) break; } delete e; e = zip.GetNextEntry(); }` — this is exactly the pattern in the sibling `tests/fuzz/tar.cpp:43-52` (tar uses the analogous `wxTarInputStream`/`wxTarEntry` API with identical shape) and in `tests/archive/ziptest.cpp:189-203` / `tests/archive/archivetest.cpp:756-775` for zip specifically.

### Core internal functions (not part of the public API surface a harness calls directly, but reached through the Entries above)
- `wxZipEntry::ReadLocal`, `wxZipEntry::ReadCentral`, `wxZipEntry::LoadExtraInfo`, `wxZipEntry::ReadDescriptor` — private-ish parsing helpers on `wxZipEntry`, invoked only from `wxZipInputStream::ReadLocal/ReadCentral/DoOpen/OnSysRead`. Not public API, not directly callable by a harness in the ordinary sense (no `public:` members under `wxZipEntry` calling these are exposed outside the class per `class WXDLLIMPEXP_BASE wxZipEntry : public wxArchiveEntry` — confirmed these are reached only via the Entries, not meant to be invoked standalone).
- `wxZipInputStream::LoadEndRecord`, `wxZipInputStream::FindEndRecord`, `wxZipInputStream::DoOpen`, `wxZipInputStream::OpenDecompressor` — all `private` members of `wxZipInputStream` (zipstrm.h:409-431: declared after the `private:` label), confirmed NOT public API; reached only indirectly via `GetNextEntry()`, `OpenEntry()`, `GetComment()`, `GetTotalEntries()`, and the `Read()`/`OnSysRead()` chain. A harness cannot and should not call them directly — this matches the task's framing of them as "Core" functions.

## Build notes
**Claim:** The fuzzer is a plain C++ translation unit, compiled with wxWidgets' own `configure`+`make` build (non-GUI, static, base-only), then linked directly against the already-built static libraries using `wx-config`'s `--cxxflags --libs base` output; the target's header is reached through the standard wx include path set up by that build, no special `-I` flags are added manually by the harness.
**Evidence:**
> ossfuzz_view/wxwidgets-c1/build.sh:18-19: `pushd $SRC/wxwidgets; ./tests/fuzz/ossfuzz.sh`
> wxwidgets/tests/fuzz/ossfuzz.sh:3-9: 
> ```
> ./configure --without-subdirs --disable-shared --disable-sys-libs --disable-gui LDFLAGS="$CXXFLAGS"
> make -j$(nproc)
> $CXX $CXXFLAGS -o $OUT/zip tests/fuzz/zip.cpp $LIB_FUZZING_ENGINE `./wx-config --cxxflags --libs base`
> ```
> wxwidgets/tests/fuzz/tar.cpp:9-11 (sibling, same include pattern expected for zip): `#include "wx/log.h" #include "wx/mstream.h" #include "wx/tarstrm.h"` — by analogy a zip harness includes `#include "wx/zipstrm.h"` and `#include "wx/mstream.h"` (for `wxMemoryInputStream` wrapping the fuzz `data`/`size`), relying on `wx-config --cxxflags` to supply `-I` for `wx/*.h`.
> Current `tests/fuzz/zip.cpp` in the tree is a stub (committed as "Gold harness replaced by an empty stub for baseline generation" — tests/fuzz/zip.cpp:1) that merely keeps `wxZipInputStream::GetNextEntry` linked via an `__asm__` alias trick, confirming the mangled symbol `_ZN16wxZipInputStream12GetNextEntryEv` is the intended fuzz target but the real harness body needs to be (re)written by the downstream Harness Generator.
> Seed corpus: `ossfuzz.sh:20`: `zip -j $OUT/zip_seed_corpus.zip tests/fuzz/corpus/zip/*` — real zip files should be used/placed as seeds.
> Language: C++ (file extension `.cpp`, uses `std::unique_ptr`, `wxString`, etc. throughout zipstrm.cpp/h) — `extern "C" int LLVMFuzzerTestOneInput(...)` entry point convention as seen in `tests/fuzz/tar.cpp:37` and `tests/fuzz/runner.cpp:24`.
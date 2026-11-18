## target: woff2::ConvertWOFF2ToTTF (const uint8_t*, size_t, WOFF2Out*)

### P2.1 Init Sequence
**Claim:** No explicit library/global init call is required. The caller must only
construct a `WOFF2Out` sink (typically `WOFF2StringOut` or `WOFF2MemoryOut`) before
calling the target; sizing that sink is commonly (but not mandatorily) done by first
calling `ComputeWOFF2FinalSize(data, length)`.
**Evidence:**
> include/woff2/decode.h:28-32: "// Decompresses the font into out. Returns true on success. ... bool ConvertWOFF2ToTTF(const uint8_t *data, size_t length, WOFF2Out* out);"
> src/woff2_decompress.cc:28-34: `std::string output(std::min(woff2::ComputeWOFF2FinalSize(raw_input, input.size()), woff2::kDefaultMaxSize), 0); woff2::WOFF2StringOut out(&output); const bool ok = woff2::ConvertWOFF2ToTTF(raw_input, input.size(), &out);`
> src/convert_woff2ttf_fuzzer_new_entry.cc:7-10: identical pattern (`ComputeWOFF2FinalSize` → `WOFF2StringOut` → `ConvertWOFF2ToTTF`) used by the project's own fuzzer.
> There is no `woff2_init()`/global setup function anywhere in the tree (searched `src/*.cc`, `include/woff2/*.h`).

### P2.2 Parameter Construction
**Claim:**
- `data` (`const uint8_t*`): pointer to the raw WOFF2 file bytes, attacker controlled, no required alignment; must be non-null and `length` bytes long.
- `length` (`size_t`): exact size of the `data` buffer; it is checked against the header's internal `reported_length` field (must match exactly) and used as the bound for every subsequent `Buffer` read.
- `out` (`WOFF2Out*`): non-null pointer to a heap/stack-allocated object implementing `WOFF2Out` (`WOFF2StringOut` or `WOFF2MemoryOut` provided by the library, or a custom subclass). Must outlive the call. `WOFF2StringOut` wraps a caller-owned `std::string*` (must stay alive); `WOFF2MemoryOut` wraps a caller-owned fixed buffer + its size (must be large enough or `Write()` calls fail and `ConvertWOFF2ToTTF` returns false).
**Evidence:**
> include/woff2/decode.h:31-32: signature.
> include/woff2/output.h:51-67: `WOFF2StringOut` ctor takes `std::string *buf`; `72-84`: `WOFF2MemoryOut` ctor takes `uint8_t* buf, size_t buf_size`.
> src/woff2_dec.cc:1084-1088: `!file.ReadU32(&reported_length) || length != reported_length` → failure if mismatched, i.e. `length` must equal the WOFF2 header's declared total length field.
> src/woff2_out.cc:21-23 / 54-56: both `Write` overloads bounds-check `offset`/`n` against `max_size_`/`buf_size_` and return `false` on overflow — callers must handle a `false` return from `ConvertWOFF2ToTTF` that can originate from an undersized `out`.

### P2.3 Object Lifecycle
**Claim:** The only "object" with a lifecycle is the `WOFF2Out` sink: Create (construct `WOFF2StringOut`/`WOFF2MemoryOut`) → (optionally configure via `SetMaxSize` for `WOFF2StringOut`) → Use (pass to `ConvertWOFF2ToTTF`, which calls `Write()`/`Size()` on it many times internally) → read final bytes via `Size()` / the backing buffer → destroy (stack-scope end or `delete`, no explicit teardown API). `WOFF2Header`/`RebuildMetadata` are stack-local, non-caller-visible structs created and destroyed entirely inside `ConvertWOFF2ToTTF`.
**Evidence:**
> src/woff2_dec.cc:1356-1396: `ConvertWOFF2ToTTF` locally creates `RebuildMetadata metadata; WOFF2Header hdr;` — callers never touch these.
> include/woff2/output.h:60-62: `Size()` and `SetMaxSize()` are the only post-construction configuration/query hooks on `WOFF2StringOut`.
> src/woff2_decompress.cc:32-38: `WOFF2StringOut out(&output);` created on the stack right before the call, read via `out.Size()` right after, goes out of scope at function end — no explicit destroy call needed (virtual dtor at output.h:35 is trivial).

### P2.4 Return Value Handling
**Claim:** `ConvertWOFF2ToTTF` returns `bool`: `true` = success, the sink now holds a complete valid SFNT; `false` = any parsing/decompression/reconstruction/output-bounds failure. On `false`, output bytes already written into the sink **are not meaningful** and must be discarded (output buffer may be partially/incorrectly filled since `WriteHeaders` and `ReconstructFont` write incrementally before failures are detected downstream). Callers must check the return value and only use `out.Size()`/the buffer when `true`.
**Evidence:**
> src/woff2_dec.cc:1360-1396: every internal step (`ReadWOFF2Header`, `WriteHeaders`, compression-ratio check, `Woff2Uncompress`, `ReconstructFont`) returns early with `FONT_COMPRESSION_FAILURE()` (which just returns `false`, optionally logging) on any error — the final `return true;` is reached only if all stages succeed.
> src/woff2_decompress.cc:34-39: `const bool ok = woff2::ConvertWOFF2ToTTF(...); if (ok) { woff2::SetFileContents(outfilename, output.begin(), output.begin() + out.Size()); } return ok ? 0 : 1;` — real caller gates all use of `out` on `ok`.
> src/convert_woff2ttf_fuzzer_new_entry.cc:10: harness calls it but (being a stub for this analysis) ignores the return — real/robust usage should check it per the tool above.

### P2.5 Cleanup Sequence
**Claim:** No library-side cleanup/free function exists for `ConvertWOFF2ToTTF`'s inputs or outputs — all internal buffers (`uncompressed_buf`, `WOFF2Header`, `RebuildMetadata`) are stack/RAII `std::vector`/`std::string`/struct objects freed automatically on return (success or failure). The caller's only owned resource is the `WOFF2Out` object and its backing storage (`std::string` or `uint8_t[]`), which are destroyed by normal C++ scope/RAII rules; no explicit "close"/"free" API is exposed for them.
**Evidence:**
> src/woff2_dec.cc:1377: `std::vector<uint8_t> uncompressed_buf(hdr.uncompressed_size);` — stack-scoped, freed automatically at function exit on both success and failure paths.
> include/woff2/output.h:33-46: `WOFF2Out` declares only a virtual destructor (`virtual ~WOFF2Out(void) {}`), no explicit `Close()`/`Free()` method.
> Searched `src/*.cc`, `include/woff2/*.h` for `Free|Destroy|Close` related to decode path: none found tied to `ConvertWOFF2ToTTF`.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase.
**Evidence:**
> include/woff2/decode.h:19,25-26,31-32: `ComputeWOFF2FinalSize`, both `ConvertWOFF2ToTTF` overloads declared.
> src/woff2_dec.cc:1339,1350,1356: all three defined.
> include/woff2/output.h:33,51,72: `WOFF2Out`, `WOFF2StringOut`, `WOFF2MemoryOut` declared; defined in src/woff2_out.cc:13-61.
> src/woff2_dec.cc:1073 (`ReadWOFF2Header`), 1256 (`WriteHeaders`) — present as static/namespace functions used internally by the target (not part of public header, internal "core").

### P2.7 Co-call Constraints
**Claim:** `ComputeWOFF2FinalSize(data, length)` and `ConvertWOFF2ToTTF(data, length, &out)` are commonly paired (size-then-decode), but this is advisory, not enforced — `ConvertWOFF2ToTTF` re-validates/derives its own sizes internally and works even if the externally computed size is wrong, because `WOFF2StringOut`/`WOFF2MemoryOut` grow/bounds-check independently. There is no lock/unlock or begin/commit pairing in this API; the only "pairing" is construct-sink → call-target → read-sink.
**Evidence:**
> include/woff2/decode.h:28-29: "Decompresses the font into out. Returns true on success. Works even if WOFF2Header totalSfntSize is wrong." — target is explicitly resilient to bad size hints.
> src/woff2_decompress.cc:28-34 and src/convert_woff2ttf_fuzzer_new_entry.cc:7-10: both real callers do call `ComputeWOFF2FinalSize` first to presize the `std::string`, confirming the idiomatic pairing even though not mandatory.
> src/woff2_dec.cc:1339-1348: `ComputeWOFF2FinalSize` reads only bytes 16-20 of the header (skip 16, ReadU32) and returns that raw `total_length` field with no cross-validation against the real table directory — it is a hint only.

### P2.8 Prerequisite State
**Claim:** No external context/connection/config object must exist beforehand (no `FontCollection`, no global state, no file handles beyond what the caller passes in `data`). The only "state" that must exist is the caller-owned output sink and the raw byte buffer `data` (commonly obtained by reading a `.woff2` file, as in `woff2_decompress.cc`).
**Evidence:**
> src/woff2_decompress.cc:26-31: `std::string input = woff2::GetFileContent(filename);` reads file into memory; `raw_input`/`input.size()` are the only prerequisites besides the `WOFF2StringOut`.
> No `struct woff2_context`/global singleton found anywhere in `src/*.cc`, `include/woff2/*.h` (searched for init/context patterns).

### P2.9 Result Usage
**Claim:** After a successful call, real callers read the result exclusively through the `WOFF2Out` interface: `Size()` to know how many bytes were written, and the underlying buffer (`std::string`/`uint8_t*`) to get the actual SFNT bytes; these are then written to a file or otherwise consumed — there is no separate "parse result" object returned by the target.
**Evidence:**
> src/woff2_decompress.cc:36-39: `if (ok) { woff2::SetFileContents(outfilename, output.begin(), output.begin() + out.Size()); }` — uses `out.Size()` to slice the valid prefix of `output` (a `std::string`) and writes it to disk via `SetFileContents` (declared in `src/file.h`).
> src/convert_woff2ttf_fuzzer_new_entry.cc:7-11: the fuzzer builds `std::string output` sized via `ComputeWOFF2FinalSize`, wraps it in `WOFF2StringOut out(&output)`, calls the target, and (in a complete harness) would use `output`/`out.Size()` afterward — in this stubbed file the result is simply discarded, but the pattern of "construct sink around buffer, call target, use buffer/out.Size() on success" is the established one.
> include/woff2/output.h:60: `size_t Size() override { return offset_; }` is the documented query API for "how many bytes were actually produced."

### P2.10 Behaviour Controls
**Claim:** The target itself takes no explicit mode flags — all branching is driven by bytes inside `data` (the untrusted input), decoded by the internal core functions:
- `hdr.flavor == kTtcFontFlavor` (`0x74746366`, "ttcf") switches `ReadWOFF2Header` into the TrueType-Collection parsing branch (`header_version` 0x00010000 vs 0x00020000, multiple `TtcFont` entries) vs. the single-font branch.
- Per-table transform flag bits (checked in the core, e.g. `kWoff2FlagsTransform`, referenced from `ReconstructFont`/`ReconstructGlyf`/`ReconstructTransformedHmtx`) decide whether a table is copied verbatim or passed through the glyf/hmtx transform reconstruction.
- `compression_ratio = hdr.uncompressed_size / length` vs. `kMaxPlausibleCompressionRatio` (100.0) rejects inputs claiming an implausible decompression ratio before calling `Woff2Uncompress`.
- Choice of `out` implementation (`WOFF2StringOut` growable vs `WOFF2MemoryOut` fixed-size) selects which bounds-check/overflow path `Write()` exercises, but does not change parsing logic.
- `ConvertWOFF2ToTTF(result, result_length, data, length)` (the deprecated overload) forces the fixed-size `WOFF2MemoryOut` path; the preferred `WOFF2Out*` overload lets the caller pick either sink type.
**Evidence:**
> src/woff2_dec.cc:1148-1160: `if (hdr->flavor == kTtcFontFlavor) { ... header_version != 0x00010000 && != 0x00020000 → fail ... }` — TTC vs single-font branch.
> src/woff2_dec.cc:67: `const float kMaxPlausibleCompressionRatio = 100.0;`
> src/woff2_dec.cc:1368-1374: ratio check and rejection before decompression.
> src/woff2_dec.cc:1350-1354: deprecated overload hard-wires `WOFF2MemoryOut out(result, result_length);`.
> src/woff2_out.cc:20-23 vs 53-56: distinct bounds-check arithmetic for `WOFF2StringOut` (`max_size_`) vs `WOFF2MemoryOut` (`buf_size_`).

### P2.11 Input Expectations
**Claim:** `data` must begin with the 4-byte WOFF2 signature `0x774f4632` ("wOF2"), be followed by a 4-byte flavor tag, and its 3rd 32-bit field ("reported length") must exactly equal the caller-supplied `length`. `num_tables` must be non-zero. Metadata/private-data blocks, if their offset field is non-zero, must satisfy `offset < length && length - offset >= declared_length` (checked explicitly, guarding against overflow). The table directory is then read with `ReadTableDirectory`, and `hdr.uncompressed_size` is derived from the last table's `src_offset + src_length` (checked for overflow via `uncompressed_size < src_offset`). For TTC-flavored input, `header_version` must be exactly `0x00010000` or `0x00020000`, `num_fonts` must be non-zero, and each font's `num_tables` must be non-zero. There is no minimum/maximum absolute size documented beyond what these field-by-field bounds checks enforce, and no required null-termination (binary format, length-delimited throughout via the `Buffer` helper class).
**Evidence:**
> src/woff2_dec.cc:1076-1080: signature check `signature != kWoff2Signature` → failure; `kWoff2Signature = 0x774f4632` (src/woff2_common.h:19).
> src/woff2_dec.cc:1084-1088: `length != reported_length` → failure.
> src/woff2_dec.cc:1089-1091: `!hdr->num_tables` → failure.
> src/woff2_dec.cc:1110-1132: meta/private offset+length overflow/bounds checks.
> src/woff2_dec.cc:1140-1144: `uncompressed_size = last_table.src_offset + last_table.src_length;` with overflow check `uncompressed_size < last_table.src_offset`.
> src/woff2_dec.cc:1148-1169: TTC header_version and per-font num_tables validation.

## Other entry: woff2::ConvertWOFF2ToTTF(uint8_t *result, size_t result_length, const uint8_t *data, size_t length)

**Claim:** Deprecated/legacy overload. Requires the caller to pre-allocate a fixed output buffer `result` of `result_length` bytes (the header comment recommends sizing it via `ComputeWOFF2FinalSize`). Internally it just builds a `WOFF2MemoryOut out(result, result_length);` and forwards to the `WOFF2Out*`-based target — identical parsing/reconstruction core, but the output path now hard-fails (`Write` returns `false`, call returns `false`) the instant `result_length` is too small, instead of growing like `WOFF2StringOut`. Returns `bool` (`true`=success, `result[0..out.Size())` valid; `false`=failure/undersized buffer). Nothing to free beyond the caller's own `result` buffer (caller-owned, caller-allocated/freed).
**Evidence:**
> include/woff2/decode.h:21-26: "// Decompresses the font into the target buffer. The result_length should be the same as determined by ComputeFinalSize() ... DEPRECATED; please prefer the version that takes a WOFF2Out*"
> src/woff2_dec.cc:1350-1354: `WOFF2MemoryOut out(result, result_length); return ConvertWOFF2ToTTF(data, length, &out);`
> src/woff2_out.cc:53-56: `if (offset > buf_size_ || n > buf_size_ - offset) { return false; }` — fixed, non-growable bound.
**How it combines with the target in a harness:** A harness exercising this overload should (a) call `ComputeWOFF2FinalSize(data, length)` to size a `std::vector<uint8_t>`/buffer (capped at `kDefaultMaxSize` to avoid huge allocations, matching real callers' pattern), (b) call this overload with that buffer, which internally re-invokes the same core path as the primary target — so calling both overloads in one harness run is redundant work on the same core logic but exercises the two different `WOFF2Out` bounds-check implementations (`WOFF2MemoryOut` vs `WOFF2StringOut`). `convert_woff2ttf_fuzzer_new_entry.cc` keeps both symbols linked (see `qf_target_0`/`qf_target_1` in `convert_woff2ttf_fuzzer.cc:10-11`) confirming both overloads belong to the same feature/entry set.

## Other entry: woff2::ComputeWOFF2FinalSize(const uint8_t *data, size_t length)

**Claim:** Pure sizing/config helper, not a parser of the full format — it only reads the WOFF2 header's `signature`(4B)+`flavor`(4B)+`reportedLength`(4B) region, specifically skipping the first 16 bytes and reading the 32-bit `total_length`/`totalSfntSize`-adjacent field at offset 16 (per code: `file.Skip(16); file.ReadU32(&total_length);`). Returns that raw `uint32_t` value widened to `size_t`, or `0` if the buffer is shorter than 20 bytes (the `Buffer` read fails). **This value is advisory only** — it is not cross-validated against the real table directory/compressed sizes, so it can be wrong or adversarially large/small; real callers always clamp it (`std::min(ComputeWOFF2FinalSize(...), kDefaultMaxSize)`) before allocating. No init needed; no cleanup (returns a scalar). Does not fail/throw other than returning 0.
**Evidence:**
> src/woff2_dec.cc:1339-1348: `size_t ComputeWOFF2FinalSize(const uint8_t* data, size_t length) { Buffer file(data, length); uint32_t total_length; if (!file.Skip(16) || !file.ReadU32(&total_length)) { return 0; } return total_length; }`
> src/woff2_decompress.cc:28-30: `std::string output(std::min(woff2::ComputeWOFF2FinalSize(raw_input, input.size()), woff2::kDefaultMaxSize), 0);` — real caller clamps the untrusted size.
> src/convert_woff2ttf_fuzzer_new_entry.cc:7-8: identical clamp pattern.
**How it combines with the target in a harness:** Call it first on the same `data`/`length` the harness will later pass to `ConvertWOFF2ToTTF`, clamp the result to `kDefaultMaxSize` (include/woff2/output.h:22), use it to size the `std::string`/buffer backing the `WOFF2Out` sink, then call the target. Because the value is adversarial-input-derived and unvalidated, it must always be clamped before allocation to avoid an OOM in the harness itself — exactly as both real callers do.

## Build notes
**Claim:** Fuzzers are plain C++ (`-std=c++11`), compiled against the project's own `include/` and internal `src/`/`brotli/c/include/` headers, then linked against the prebuilt `.o` objects (including brotli decoder objects) packed into a static archive, and finally linked with `$LIB_FUZZING_ENGINE`.
**Evidence:**
> Makefile:3: `CPPFLAGS = -I./brotli/c/include/ -I./src -I./include` — target's header `woff2/decode.h` is reachable via `-I./include` (`#include <woff2/decode.h>` as seen in src/woff2_decompress.cc:13 and convert_woff2ttf_fuzzer_new_entry.cc:2).
> Makefile:25: `CXXFLAGS += $(COMMON_FLAGS) -std=c++11` — C++11.
> Makefile:42-53: `ARCHIVES=convert_woff2ttf_fuzzer convert_woff2ttf_fuzzer_new_entry` ... `$(ARCHIVES) : $(ARCHIVE_OBJS) $(OBJS) deps` → `$(AR) $(ARFLAGS) $(SRCDIR)/$@.a $(OBJS) $(COMMONOBJ) $(ENCOBJ) $(DECOBJ) $(SRCDIR)/$@.o` — bundles woff2 core objects + brotli common/enc/dec objects + the fuzzer's own `.o` into a `.a` archive.
> ${QF_WORK}/ossfuzz_view/woff2-c1/build.sh:37-45: `make CC="$CC $CFLAGS" CXX="$CXX $CXXFLAGS" ... all` then `for fuzzer_archive in src/*fuzzer*.a; do $CXX $CXXFLAGS $LIB_FUZZING_ENGINE $fuzzer_archive -o $OUT/$fuzzer_name; ... done` — final link step combines the archive with libFuzzer's main via `$LIB_FUZZING_ENGINE`.
> ${QF_WORK}/ossfuzz_view/woff2-c1/Dockerfile:17-22: based on `gcr.io/oss-fuzz-base/base-builder`, clones `google/woff2` recursively (pulls in the `brotli` submodule used for decompression).
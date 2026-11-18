## target: woff2::ConvertWOFF2ToTTF

### P2.0 Main path to keep (every input) — read first
**Claim:** One decode per input through the growable sink: `hint = ComputeWOFF2FinalSize(data, size)` → `std::string buf` presized only if the hint is small (≤ 1 MiB), otherwise empty → `WOFF2StringOut out(&buf); out.SetMaxSize(30 MiB)` → `ConvertWOFF2ToTTF(data, size, &out)` on the full input → on success read `out.Size()` bytes of `buf`. No per-input buffer is ever sized from the hint beyond that small bound; the fixed-buffer overload is not part of the main path.
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> src/woff2_dec.cc:1339-1348: `size_t ComputeWOFF2FinalSize(...) { ... if (!file.Skip(16) || !file.ReadU32(&total_length)) return 0; return total_length; }` — an untrusted u32 straight from the header.
> src/woff2_decompress.cc:27-38: real caller: `std::string output(std::min(ComputeWOFF2FinalSize(...), woff2::kDefaultMaxSize), 0); woff2::WOFF2StringOut out(&output); const bool ok = woff2::ConvertWOFF2ToTTF(raw_input, input.size(), &out); if (ok) SetFileContents(..., output.begin(), output.begin() + out.Size());` — presizing is a CLI convenience; under fuzzing kDefaultMaxSize (128 MiB, include/woff2/output.h:22) would be zero-filled per input.
> include/woff2/output.h:51-62: `WOFF2StringOut` — "buf may be sized (e.g. using EstimateWOFF2FinalSize) or empty", `Size()`, `SetMaxSize()`.
> src/woff2_out.cc:20-35: `WOFF2StringOut::Write` appends / grows `buf_` up to `max_size_`, so an empty string is valid.
> src/woff2_dec.cc:1350-1354: the 4-argument overload is `WOFF2MemoryOut out(result, result_length); return ConvertWOFF2ToTTF(data, length, &out);` — same decoder, different sink.
> src/woff2_dec.cc:1368-1377: compression-ratio guard then `std::vector<uint8_t> uncompressed_buf(hdr.uncompressed_size)` before brotli `Woff2Uncompress` — reached only after the header parses.


### Declaration & definitions
- Declared `include/woff2/decode.h:31`: `bool ConvertWOFF2ToTTF(const uint8_t *data, size_t length, WOFF2Out* out);` — "Decompresses the font into out. Returns true on success. Works even if WOFF2Header totalSfntSize is wrong. Please prefer this API." (`include/woff2/decode.h:28-32`)
- Deprecated overload declared `include/woff2/decode.h:25-26`: `bool ConvertWOFF2ToTTF(uint8_t *result, size_t result_length, const uint8_t *data, size_t length);` "DEPRECATED; please prefer the version that takes a WOFF2Out*" (`include/woff2/decode.h:21-24`)
- `ComputeWOFF2FinalSize` declared `include/woff2/decode.h:19`: `size_t ComputeWOFF2FinalSize(const uint8_t *data, size_t length);` "Compute the size of the final uncompressed font, or 0 on error."
- Definitions all in `src/woff2_dec.cc`:
  - `ComputeWOFF2FinalSize`: `src/woff2_dec.cc:1339-1348`
  - deprecated `ConvertWOFF2ToTTF(result,result_length,data,length)`: `src/woff2_dec.cc:1350-1354`
  - main `ConvertWOFF2ToTTF(data,length,out)`: `src/woff2_dec.cc:1356-1396`

### Body of the target (ConvertWOFF2ToTTF(data,length,out))
```
src/woff2_dec.cc:1358-1396
  RebuildMetadata metadata;
  WOFF2Header hdr;
  if (!ReadWOFF2Header(data, length, &hdr)) return FONT_COMPRESSION_FAILURE();
  if (!WriteHeaders(data, length, &metadata, &hdr, out)) return FONT_COMPRESSION_FAILURE();
  compression_ratio = hdr.uncompressed_size / length;
  if (compression_ratio > kMaxPlausibleCompressionRatio) return FONT_COMPRESSION_FAILURE();  // =100.0, src/woff2_dec.cc:67
  src_buf = data + hdr.compressed_offset;
  uncompressed_buf.resize(hdr.uncompressed_size);
  if (hdr.uncompressed_size < 1) return FONT_COMPRESSION_FAILURE();
  if (!Woff2Uncompress(&uncompressed_buf[0], hdr.uncompressed_size, src_buf, hdr.compressed_length))
      return FONT_COMPRESSION_FAILURE();
  for each font_info in metadata.font_infos:
      if (!ReconstructFont(&uncompressed_buf[0], hdr.uncompressed_size, &metadata, &hdr, i, out))
          return FONT_COMPRESSION_FAILURE();
  return true;
```
`ConvertWOFF2ToTTF` itself is self-contained: it allocates/owns `RebuildMetadata` and `WOFF2Header` as locals and frees them automatically on return (no manual cleanup needed by the caller for these).

### P2.1 Init Sequence
**Claim:** No library-global init call is required. The only "prerequisite" is constructing a `WOFF2Out`-derived sink object (`WOFF2StringOut` or `WOFF2MemoryOut`) and, if you want to size it correctly, calling `ComputeWOFF2FinalSize(data, length)` first to learn/allocate the output buffer. There is no separate "open/create decoder" step — `ConvertWOFF2ToTTF` is a single stateless call.
**Evidence:**
> src/woff2_decompress.cc:26-34 (real CLI driver):
> ```
> std::string input = woff2::GetFileContent(filename);
> const uint8_t* raw_input = ...;
> std::string output(std::min(woff2::ComputeWOFF2FinalSize(raw_input, input.size()), woff2::kDefaultMaxSize), 0);
> woff2::WOFF2StringOut out(&output);
> const bool ok = woff2::ConvertWOFF2ToTTF(raw_input, input.size(), &out);
> ```
> src/convert_woff2ttf_fuzzer.cc:7-12 (project's own fuzzer — does NOT call ComputeWOFF2FinalSize, just sizes via SetMaxSize):
> ```
> std::string buf;
> woff2::WOFF2StringOut out(&buf);
> out.SetMaxSize(30 * 1024 * 1024);
> woff2::ConvertWOFF2ToTTF(data, size, &out);
> ```
> include/woff2/decode.h:29: "Works even if WOFF2Header totalSfntSize is wrong" — confirms ComputeWOFF2FinalSize's result is only a sizing hint, not a correctness requirement for the main overload.

### P2.2 Parameter Construction
**Claim:** `data`/`length` are the raw untrusted WOFF2 file bytes and its size — any buffer/length pair is accepted (the function itself validates format). `out` must be a non-null pointer to a live `WOFF2Out` subclass instance (`WOFF2StringOut` wrapping a `std::string*`, or `WOFF2MemoryOut` wrapping a fixed `uint8_t*`/size, or a custom subclass). For the deprecated overload, `result`/`result_length` must point to a buffer at least as large as `ComputeWOFF2FinalSize(data,length)` returns (see doc comment) — if too small, `WOFF2MemoryOut::Write` will fail safely (bounds-checked) and the call returns false, but should not overflow.
**Evidence:**
> include/woff2/decode.h:21-23: "Decompresses the font into the target buffer. The result_length should be the same as determined by ComputeFinalSize()."
> include/woff2/output.h:72-84: `WOFF2MemoryOut(uint8_t* buf, size_t buf_size)` constructor signature — takes a fixed-size buffer and its size.
> include/woff2/output.h:51-67: `WOFF2StringOut(std::string *buf)` — "buf may be sized (e.g. using EstimateWOFF2FinalSize) or empty", so a default-constructed empty string is also valid, matching the fuzzer usage above.
> src/convert_woff2ttf_fuzzer_new_entry.cc:10-11: mangled-symbol references confirm both overloads exist and are the only two decls: `_ZN5woff217ConvertWOFF2ToTTFEPKhmPNS_8WOFF2OutE` (data,length,out) and `_ZN5woff217ConvertWOFF2ToTTFEPhmPKhm` (result,result_length,data,length).

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: (1) create the output sink object (`WOFF2StringOut`/`WOFF2MemoryOut`), optionally configure it (`SetMaxSize`), (2) call `ConvertWOFF2ToTTF(data, length, &out)`, (3) on success, read results via `out.Size()` and the underlying buffer (`output.begin()`, or the fixed buffer passed to `WOFF2MemoryOut`), (4) destroy the sink (automatic, stack-scoped in all real callers — no explicit destroy call seen). `ConvertWOFF2ToTTF` does not retain the sink beyond the call.
**Evidence:**
> src/woff2_decompress.cc:32-38: `WOFF2StringOut out(&output);` ... `ConvertWOFF2ToTTF(...)` ... `if (ok) SetFileContents(outfilename, output.begin(), output.begin() + out.Size());` — sink is stack-local, used immediately after the call, then goes out of scope at function end (implicit destroy).
> src/convert_woff2ttf_fuzzer.cc:7-12: same stack-local pattern, sink destroyed when `TestOneInput` returns.

### P2.4 Return Value Handling
**Claim:** Return is `bool`: `true` = success (output fully written and `out->Size()`/buffer valid), `false` = failure at any validation/decoding step; callers must check it before trusting/using the output buffer, and must not assume partial output in the sink is valid/complete on failure.
**Evidence:**
> src/woff2_dec.cc:1360-1395: every internal failure path returns `FONT_COMPRESSION_FAILURE()` (a macro that ultimately yields `false`), e.g. `if (!ReadWOFF2Header(...)) return FONT_COMPRESSION_FAILURE();`, `if (!Woff2Uncompress(...)) return FONT_COMPRESSION_FAILURE();`, `if (!ReconstructFont(...)) return FONT_COMPRESSION_FAILURE();`; only the final `return true;` (src/woff2_dec.cc:1395) signals success.
> src/woff2_decompress.cc:34-40: `const bool ok = woff2::ConvertWOFF2ToTTF(...); if (ok) { SetFileContents(...); } return ok ? 0 : 1;` — canonical check-before-use pattern.

### P2.5 Cleanup Sequence
**Claim:** No explicit free/destroy calls exist for `ConvertWOFF2ToTTF` itself: `WOFF2Header`/`RebuildMetadata` are stack locals inside the function (freed automatically on every return path, success or failure, via RAII/vector destructors). The caller-owned `WOFF2Out` sink (and its backing `std::string`/buffer) is the only object the harness controls; it is cleaned up by normal C++ scope exit (`std::string` destructor, or caller-managed raw buffer) in every real caller found — no manual `delete`/`free` of the sink is performed in-library.
**Evidence:**
> src/woff2_dec.cc:1358-1359: `RebuildMetadata metadata; WOFF2Header hdr;` declared as locals, no heap alloc/manual free.
> src/woff2_decompress.cc:22-40: `std::string output(...)`, `woff2::WOFF2StringOut out(&output);` both stack-scoped; no explicit destructor calls.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> include/woff2/decode.h:19,25,31 — `ComputeWOFF2FinalSize`, both `ConvertWOFF2ToTTF` overloads declared.
> src/woff2_dec.cc:1339,1350,1356 — all three defined.
> include/woff2/output.h:33,51,72 — `WOFF2Out`, `WOFF2StringOut`, `WOFF2MemoryOut` classes defined.
> src/woff2_dec.cc:406,701,798,923,1073,1256 — `ReconstructGlyf`, `ReconstructTransformedHmtx`, `Woff2Uncompress`, `ReconstructFont`, `ReadWOFF2Header`, `WriteHeaders` all defined (internal, in anonymous or woff2 namespace in same .cc file; `ReadWOFF2Header`/`WriteHeaders`/`ReconstructFont`/`ReconstructGlyf`/`ReconstructTransformedHmtx`/`Woff2Uncompress` are not declared in any installed header, confirming they are internal "core" functions, not part of the public API surface a harness should call directly).

### P2.7 Co-call Constraints
**Claim:** The only paired/co-call relationship is sink-construction before, and result-read after, the single `ConvertWOFF2ToTTF` call: `WOFF2StringOut/WOFF2MemoryOut` must be constructed before the call and its `Size()`/buffer read only after a `true` return. There is no begin/end, lock/unlock, or open/close pair around `ConvertWOFF2ToTTF` itself — it is a single idempotent call per input. `ComputeWOFF2FinalSize` is an optional helper that may be called before `ConvertWOFF2ToTTF` purely to size the output buffer; it is not required to pair 1:1 (the project's own fuzzer skips it, see below).
**Evidence:**
> src/woff2_decompress.cc:28-34: `ComputeWOFF2FinalSize` → buffer sizing → `WOFF2StringOut` construction → `ConvertWOFF2ToTTF` call, in that order.
> src/convert_woff2ttf_fuzzer.cc:7-11: `WOFF2StringOut out(&buf); out.SetMaxSize(30*1024*1024); ConvertWOFF2ToTTF(data,size,&out);` — no `ComputeWOFF2FinalSize` call at all, showing it's optional, not a mandatory pairing.

### P2.8 Prerequisite State
**Claim:** No external context/connection/global config is required. The only prerequisite "state" is the caller-allocated `WOFF2Out` sink object existing and being empty/large-enough before the call (per `WOFF2StringOut`/`WOFF2MemoryOut` doc comments). The raw `data`/`length` need not be pre-validated — the target performs all format validation internally (signature check, length checks, etc. in `ReadWOFF2Header`, src/woff2_dec.cc:1077-1144).
**Evidence:**
> include/woff2/output.h:54-55: "buf->size() will grow to at most max_size; buf may be sized ... or empty." — confirms no particular pre-state of the buffer content is required, only existence.
> src/woff2_dec.cc:1076-1080: `ReadWOFF2Header` itself checks `signature != kWoff2Signature` and fails cleanly — i.e., the target expects to receive completely unvalidated bytes and validates them itself; no pre-check by the caller is expected or seen in any call site.

### P2.9 Result Usage
**Claim:** After a `true` return, callers read the produced SFNT/TTF bytes from the sink: for `WOFF2StringOut`, via `out.Size()` and the backing `std::string`'s data (`output.begin()`/`.data()`); for `WOFF2MemoryOut`, the caller's own fixed buffer is filled in place and `out.Size()` reports how many bytes were written. The typical downstream API is writing these bytes to a file or a test fixture; no other woff2 API in this codebase consumes the output further.
**Evidence:**
> src/woff2_decompress.cc:36-38: `if (ok) { woff2::SetFileContents(outfilename, output.begin(), output.begin() + out.Size()); }` — real caller writes the resulting TTF bytes to a file using `out.Size()` to bound the valid region.
> include/woff2/output.h:60,79: `size_t Size() override { return offset_; }` defined identically for both sink subclasses — this is the canonical "how many valid bytes were produced" query used by every caller.
> src/convert_woff2ttf_fuzzer.cc:11-12: the fuzzer discards the boolean and does not inspect `out`/`buf` further — it only exercises memory-safety of the conversion itself, not downstream consumption; this is consistent with the target being the terminal attack surface for this harness (no further "parse the TTF" step exists in this project).

### P2.10 Behaviour Controls
**Claim:** Behaviour of `ConvertWOFF2ToTTF` is entirely driven by *bytes inside `data`* (there are no caller-supplied flags/mode parameters on the target itself); the only caller-controlled knob is the choice of sink type/size:
  - Sink choice: `WOFF2StringOut` (growable, bounded by `SetMaxSize`, default `kDefaultMaxSize` = 128MiB) vs `WOFF2MemoryOut` (fixed caller buffer, used only via the deprecated 4-arg overload) — selects a different bounds-check / growth code path in `Write()`.
  - Within the input: `hdr.flavor == kTtcFontFlavor` selects the TTC-collection parsing branch (multiple fonts) vs single-font plain WOFF2 (`src/woff2_dec.cc:1148-1170` in `ReadWOFF2Header`); `hdr.header_version` (0x00010000 vs 0x00020000) selects whether DSIG fields are reserved in `WriteHeaders` (`src/woff2_dec.cc:1293-1297`); per-table transform flag bits select raw-copy vs glyf/hmtx-transform reconstruction in `ReconstructFont` (dispatch to `ReconstructGlyf`/`ReconstructTransformedHmtx` per table around `src/woff2_dec.cc:1006-1019`); the declared `compressed_length`/`uncompressed_size` vs actual `length` trips the `kMaxPlausibleCompressionRatio` (100.0, src/woff2_dec.cc:67) guard at src/woff2_dec.cc:1368-1374 before `Woff2Uncompress` is even attempted.
**Evidence:**
> include/woff2/output.h:61-62: `size_t MaxSize(); void SetMaxSize(size_t max_size);` on `WOFF2StringOut` — explicit caller-settable behaviour knob, used in src/convert_woff2ttf_fuzzer.cc:10 (`out.SetMaxSize(30 * 1024 * 1024)`).
> src/woff2_dec.cc:1148-1155: `if (hdr->flavor == kTtcFontFlavor) { ...read header_version...; if (header_version != 0x00010000 && != 0x00020000) FONT_COMPRESSION_FAILURE(); }` — flavor/version gate the TTC path.
> src/woff2_dec.cc:1368-1374: compression-ratio guard reading `hdr.uncompressed_size` (attacker-controlled) against `length`.

**Additional behaviour controls (previously missing):**
- Per-input cost is a behaviour control here: `ComputeWOFF2FinalSize` returns an attacker-controlled u32 (woff2_dec.cc:1339-1348). Any buffer sized from it must be capped small (≤ 1 MiB); otherwise most random inputs allocate and zero tens of MB, cutting exec/s ~5x (observed 489 vs 2389 exec/s) and the fuzzer never reaches the brotli decoder.
- The fixed-buffer overload / `WOFF2MemoryOut` is an alternative sink of the same decoder (woff2_dec.cc:1350-1354): extra step on a rare selector only, with a small buffer.

### P2.11 Input Expectations
**Claim:** Target expects a byte buffer starting with the fixed WOFF2 header: `uint32 signature` (must equal `kWoff2Signature`), `uint32 flavor`, `uint32 reported_length` (must exactly equal the caller-supplied `length` argument — this is a strict equality check, not just an upper bound), `uint16 num_tables` (must be nonzero), reserved/total_sfnt_size fields (skipped, not trusted), `uint32 compressed_length`, version fields, `meta_offset/meta_length/meta_length_orig`, `priv_offset/priv_length` (each offset+length pair bounds-checked against `length`), followed by a table directory (`ReadTableDirectory`, using variable-length `Read255UShort` encodings) and, only if `flavor == kTtcFontFlavor`, a TTC collection sub-header. No explicit minimum/maximum size constant is enforced beyond these structural checks and the `kMaxPlausibleCompressionRatio` sanity check on `uncompressed_size/length`; `hdr.uncompressed_size` must be >= 1. There is no required NUL-termination (binary format, length-prefixed throughout).
**Evidence:**
> src/woff2_dec.cc:1076-1080: `if (!file.ReadU32(&signature) || signature != kWoff2Signature || !file.ReadU32(&hdr->flavor)) return FONT_COMPRESSION_FAILURE();`
> src/woff2_dec.cc:1084-1088: `if (!file.ReadU32(&reported_length) || length != reported_length) return FONT_COMPRESSION_FAILURE();` — strict length match requirement.
> src/woff2_dec.cc:1089-1091: `if (!file.ReadU16(&hdr->num_tables) || !hdr->num_tables) return FONT_COMPRESSION_FAILURE();`
> src/woff2_dec.cc:1115-1132: meta_offset/meta_length and priv_offset/priv_length bounds-checks against `length`.
> src/woff2_dec.cc:1140-1144: `hdr->uncompressed_size = last_table.src_offset + last_table.src_length; if (hdr->uncompressed_size < last_table.src_offset) return FONT_COMPRESSION_FAILURE();` (overflow check).
> src/woff2_dec.cc:1378-1380: `if (hdr.uncompressed_size < 1) return FONT_COMPRESSION_FAILURE();`
> src/woff2_dec.cc:1368-1374: compression-ratio plausibility check.

## Other entries in the Logic Group

### Entry: woff2::ComputeWOFF2FinalSize(data, length)
**Call protocol:** No prerequisites — it is a pure, stateless read of the WOFF2 header. Takes the same raw `data`/`length` that will later be passed to `ConvertWOFF2ToTTF`. Returns `0` on error (malformed/too-short header), otherwise the attacker-declared `totalSfntSize` field (untrusted). Nothing to free (no allocation). **Must be called before constructing the output buffer/sink if the caller wants a right-sized buffer**, but per the target's own doc comment ("Works even if WOFF2Header totalSfntSize is wrong", include/woff2/decode.h:29) it is **not required** — `ConvertWOFF2ToTTF` works correctly even if this value is never queried or is wrong; `WOFF2StringOut` can start empty and grow. In the harness, combine as: call `ComputeWOFF2FinalSize(data,length)`, clamp with `std::min(..., kDefaultMaxSize)`, allocate a `std::string`/buffer of that size, then feed the *same* `data,length` into `ConvertWOFF2ToTTF`.
**Evidence:**
> include/woff2/decode.h:18-19: "// Compute the size of the final uncompressed font, or 0 on error." `size_t ComputeWOFF2FinalSize(const uint8_t *data, size_t length);`
> src/woff2_dec.cc:1339-1348:
> ```
> size_t ComputeWOFF2FinalSize(const uint8_t* data, size_t length) {
>   Buffer file(data, length);
>   uint32_t total_length;
>   if (!file.Skip(16) || !file.ReadU32(&total_length)) return 0;
>   return total_length;
> }
> ```
> src/woff2_decompress.cc:28-31: real caller combining it with buffer construction: `std::string output(std::min(woff2::ComputeWOFF2FinalSize(raw_input, input.size()), woff2::kDefaultMaxSize), 0);`
> include/woff2/output.h:22: `const size_t kDefaultMaxSize = 128 * 1024 * 1024;` — the clamp constant used alongside it.
> No caller in the tree feeds `ComputeWOFF2FinalSize`'s result into anything except buffer sizing (Grep of `ComputeWOFF2FinalSize` across the tree shows only `src/woff2_dec.cc` definition and `src/woff2_decompress.cc:29` usage); the fuzzer (`src/convert_woff2ttf_fuzzer.cc`) does not call it at all, confirming it is optional/advisory, not a required init step.

### Entry: woff2::ConvertWOFF2ToTTF(uint8_t *result, size_t result_length, const uint8_t *data, size_t length)  — deprecated fixed-buffer overload
**Call protocol:** Documented as DEPRECATED, "please prefer the version that takes a WOFF2Out*" (include/woff2/decode.h:24). It is a thin wrapper: constructs a `WOFF2MemoryOut out(result, result_length)` internally and forwards to the main `ConvertWOFF2ToTTF(data, length, &out)` — so its lifecycle, parameter validation, and return semantics are identical to the target, except the sink is always a fixed-size buffer rather than a growable one. `result` must be a valid, non-null buffer of at least `result_length` bytes, owned/allocated by the caller (commonly sized via `ComputeWOFF2FinalSize`, per its own doc comment at include/woff2/decode.h:21-23: "The result_length should be the same as determined by ComputeFinalSize()"). Nothing internal to free — `WOFF2MemoryOut` is a function-local stack object, destroyed on return. Returns `bool` with the same success/failure semantics as the target. In a harness exercising both entries "together" (per the task description), call this overload with a `result` buffer sized by `ComputeWOFF2FinalSize(data,length)` (or a capped/fuzzed size to also exercise the "buffer too small" failure path in `WOFF2MemoryOut::Write`), as a second/alternate way to drive the identical core decode (`ReadWOFF2Header`→`WriteHeaders`→`Woff2Uncompress`→`ReconstructFont`) through the fixed-buffer sink instead of the growable-string sink.
**Evidence:**
> include/woff2/decode.h:21-26: full doc comment and declaration.
> src/woff2_dec.cc:1350-1354:
> ```
> bool ConvertWOFF2ToTTF(uint8_t *result, size_t result_length,
>                        const uint8_t *data, size_t length) {
>   WOFF2MemoryOut out(result, result_length);
>   return ConvertWOFF2ToTTF(data, length, &out);
> }
> ```
> include/woff2/output.h:72-84: `WOFF2MemoryOut` ctor `(uint8_t* buf, size_t buf_size)`, `Write` overloads bounds-checked against `buf_size_` (declaration only shown here; implementation is in src/woff2_out.cc, not re-verified line-by-line but class contract documented at include/woff2/output.h:69-71 "Fixed memory block for woff2 out.").
> src/convert_woff2ttf_fuzzer_new_entry.cc:11: `_ZN5woff217ConvertWOFF2ToTTFEPhmPKhm` — confirms this exact overload's mangled symbol is present and linked in the OSS-Fuzz build, i.e. it is live code reachable from the binary.
> No direct real-world call site of this exact 4-arg overload was found elsewhere in the tree besides its self-forwarding definition (Grep for `ConvertWOFF2ToTTF(` across src/ shows only the definitions in src/woff2_dec.cc and the fuzzer-keepalive reference) — all first-party CLI/fuzzer code (`src/woff2_decompress.cc`, `src/convert_woff2ttf_fuzzer.cc`) uses the `WOFF2Out*` overload instead, reinforcing the "prefer WOFF2Out*" guidance.

### Combining the entries in one harness
**Claim:** A harness exercising this Logic Group should, per fuzzed input bytes `data,length`: (1) optionally call `ComputeWOFF2FinalSize(data,length)` to obtain a sizing hint (clamped to a sane max, e.g. `kDefaultMaxSize`), (2) call the main target `ConvertWOFF2ToTTF(data, length, &out)` with a `WOFF2StringOut` sink (optionally `SetMaxSize`), checking the `bool` return before touching `out.Size()`/the string buffer, and (3) separately also call the deprecated overload `ConvertWOFF2ToTTF(result, result_length, data, length)` with a `WOFF2MemoryOut`-backed buffer (e.g. sized from step 1, or deliberately undersized to hit the fixed-buffer failure path), again checking the `bool` return before using `result`. Both calls are independent (no shared state), so they can run back-to-back on the same `data,length` in one `LLVMFuzzerTestOneInput` to cover both sink implementations and the deprecated-API code path, mirroring the project's real CLI tool (`src/woff2_decompress.cc`) for the first and the library's own self-test pattern (`src/woff2_dec.cc:1350-1354`) for the second.
**Evidence:** see call-site evidence quoted above (src/woff2_decompress.cc:26-40, src/convert_woff2ttf_fuzzer.cc:7-12, src/woff2_dec.cc:1339-1396).

## Build notes

**Claim:** Fuzzers are plain C++11 TUs compiled against the project's headers (`-I./include`, `-I./src`, `-I./brotli/c/include/`) and the full decoder/transform/brotli object set, archived into a static `.a` and then linked by OSS-Fuzz's `build.sh` with `$LIB_FUZZING_ENGINE`.
**Evidence:**
> Makefile:3: `CPPFLAGS = -I./brotli/c/include/ -I./src -I./include` — target's header `woff2/decode.h` is reachable via `-I./include` as `#include <woff2/decode.h>` (matches `src/woff2_decompress.cc:13` and `src/convert_woff2ttf_fuzzer.cc` includes).
> Makefile:25: `CXXFLAGS += $(COMMON_FLAGS) -std=c++11` — C++11, matches `WOFF2Out` virtual-class / `std::string`/`std::vector` usage seen in src/woff2_dec.cc and include/woff2/output.h.
> Makefile:29-31: `OUROBJ = font.o glyph.o normalize.o table_tags.o transform.o woff2_dec.o woff2_enc.o woff2_common.o woff2_out.o variable_length.o` — the object set a fuzzer is linked against, including `woff2_dec.o` (target's TU) and `woff2_out.o` (sink classes' TU).
> Makefile:33-37: brotli static objects (`$(BROTLIOBJ)/enc`, `/dec`, `/common`) are separately built via `$(MAKE) -C $(BROTLI) lib` and linked in — matches the target's internal dependency on brotli decode (`Woff2Uncompress`, src/woff2_dec.cc:798).
> Makefile:42-53: `ARCHIVES = convert_woff2ttf_fuzzer convert_woff2ttf_fuzzer_new_entry` built as `.a` archives: `$(AR) $(ARFLAGS) $(SRCDIR)/$@.a $(OBJS) $(COMMONOBJ) $(ENCOBJ) $(DECOBJ) $(SRCDIR)/$@.o`.
> ${QF_WORK}/ossfuzz_view/woff2-c2/build.sh:36-45: OSS-Fuzz driver does `make ... all` then for each `src/*fuzzer*.a` archive: `$CXX $CXXFLAGS $LIB_FUZZING_ENGINE $fuzzer_archive -o $OUT/$fuzzer_name` — i.e. the fuzzer entry point (`LLVMFuzzerTestOneInput` in e.g. `src/convert_woff2ttf_fuzzer.cc`) is compiled as part of the archive and linked against `$LIB_FUZZING_ENGINE` (libFuzzer or equivalent) plus all the above objects, no separate shared library — this is a static, whole-program link.
> ${QF_WORK}/ossfuzz_view/woff2-c2/Dockerfile:17-22: base image `gcr.io/oss-fuzz-base/base-builder`; sources cloned via `git clone --depth 1 --recursive https://github.com/google/woff2` (recursive, to pull the `brotli` submodule referenced by the Makefile).
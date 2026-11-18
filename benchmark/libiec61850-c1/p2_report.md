## target: MmsValue_decodeMmsData

### P2.1 Init Sequence
**Claim:** No library-level init call (no `*_init`, no connection/context object) is required before `MmsValue_decodeMmsData`. It is a pure, self-contained BER decoder that only needs a raw byte buffer. All real call sites invoke it directly without any preceding setup other than having the input bytes/length available.
**Evidence:**
> src/mms/iso_mms/server/mms_access_result.c:352-356: `MmsValue_decodeMmsData(...) { return MmsValue_decodeMmsDataMaxRecursion(buffer, bufPos, bufferLength, endBufPos, 25); }` — thin wrapper, no global state touched.
> fuzz/fuzz_mms_encode.c:8-11 and fuzz/fuzz_mms_print.c:9-12: both harnesses call it as the very first statement on the raw fuzzer input, no init calls precede it.
> src/mms/iso_mms/client/mms_client_journals.c:77: `journalVariable->value = MmsValue_decodeMmsData(buffer, bufPos, bufPos + length, NULL);` called directly inside a BER-walking loop, no extra init.

### P2.2 Parameter Construction
**Claim:** `buffer` is a writable/readable `uint8_t*` pointing at the MMS-Data BER element; `bufPos` is the start offset inside `buffer` (0 in all fuzz/driver call sites seen); `bufferLength` is the **absolute end offset** (not a length relative to bufPos) used as the upper bound for all internal bound checks — callers pass `size` (whole buffer) or `bufPos + length` (end of sub-element), never a value smaller than `bufPos`; `endBufPos` is an optional `int*` out-parameter (NULL accepted) receiving the position right after the decoded element.
**Evidence:**
> src/mms/inc/mms_value.h:1011-1019 (doc comment): "bufferLength the length of the buffer" / "endBufPos the position in the buffer after the read MMS data element (NULL if not required)".
> src/mms/iso_mms/server/mms_access_result.c:165-167: `int dataEndBufPos = bufferLength; if (bufferLength < 1) goto exit_with_error;` — bufferLength is used directly as the end offset, and must be >=1.
> fuzz/fuzz_mms_encode.c:11: `MmsValue_decodeMmsData(data, 0, size, &out);` — bufferLength == whole input size.
> src/mms/iso_mms/client/mms_client_journals.c:77: `MmsValue_decodeMmsData(buffer, bufPos, bufPos + length, NULL)` — bufferLength computed as bufPos+length (absolute end), endBufPos passed as NULL (caller doesn't need continuation point here).
> src/mms/iso_mms/server/mms_write_service.c:317: `MmsValue_decodeMmsData(buffer, dataBufPos, length, &dataBufPos)` — endBufPos reused as the loop's own position variable to advance to the next sibling element.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (via decode) → optionally traverse/query (printToBuffer, getElement/getArraySize/getType) → optionally re-encode (encodeMmsData) → destroy (MmsValue_delete). There is no separate "configure" step; the returned `MmsValue*` is immediately usable or immediately freed on failure/NULL.
**Evidence:**
> fuzz/fuzz_mms_print.c:12-27: decode → (size-only) encodeMmsData probe → printToBuffer → MmsValue_delete.
> fuzz/fuzz_mms_encode.c:11-32: decode → encodeMmsData(size-only) → malloc buffer → encodeMmsData(real) → free buffer → MmsValue_delete(mmsValue).
> examples/server_example_logging/server_example_logging.c:100-108: decode → printToBuffer → MmsValue_delete(value).

### P2.4 Return Value Handling
**Claim:** Return value is `NULL` on any decode error (malformed tag/length, depth exceeded, oversized/zero length where disallowed, truncated buffer); callers must check for NULL before using the result and must not call MmsValue_delete on a NULL result they already know is NULL (delete is NULL-safe anyway) nor attempt further decode of leftover bytes. On success the endBufPos out-param (if non-NULL) is only written on success paths.
**Evidence:**
> src/mms/iso_mms/server/mms_access_result.c:333-343: `if (endBufPos != NULL) *endBufPos = bufPos; return value; exit_with_error: if (value != NULL) MmsValue_delete(value); return NULL;` — endBufPos is set only on the success path; NULL return on error after freeing any partially built value.
> fuzz/fuzz_mms_encode.c:12-14 / fuzz/fuzz_mms_print.c:13-15: `if (mmsValue == NULL) { return 0; }` immediately after the decode call — canonical consumer pattern.
> src/mms/iso_mms/client/mms_client_journals.c:75-78 / mms_write_service.c:319 (`if (newValue != NULL) {...} else {...}` at line 319): consumers branch on NULL to detect decode failure and abort the enclosing parse/response with an error PDU.
> src/mms/iso_mms/common/mms_value.c:1390-1392: `MmsValue_delete(MmsValue* self) { if (self == NULL) return; ... }` — delete is NULL-tolerant, but callers still check before using the value.

### P2.5 Cleanup Sequence
**Claim:** The single owned resource is the returned `MmsValue*` tree (and everything inside it: bitstring/octetstring/visiblestring buffers, integer Asn1PrimitiveValue, nested array/structure component arrays). It must be freed exactly once via `MmsValue_delete()`, which recursively frees all nested components and type-specific buffers; no other explicit free calls are needed for the decode's own output (no separate buffer alloc visible to the caller). On decode error, the decoder itself already frees any partially-built value before returning NULL, so the caller must NOT double-free.
**Evidence:**
> src/mms/iso_mms/common/mms_value.c:1389-1431: MmsValue_delete frees integer/bitString buf/octetString buf/visibleString buf, and recursively deletes MMS_ARRAY/MMS_STRUCTURE components before freeing the components array and finally `self`.
> src/mms/iso_mms/server/mms_access_result.c:340-341 (`if (value != NULL) MmsValue_delete(value);` inside `exit_with_error:`): internal cleanup on error — caller sees only NULL, nothing left to free from a failed call.
> fuzz/fuzz_mms_encode.c:30-32 / fuzz/fuzz_mms_print.c:25-27: both harnesses call `MmsValue_delete(mmsValue)` exactly once at the end, after all use of the value, as the only cleanup for the decode result.

### P2.6 API Existence
**Claim:** All functions cited below exist in the current source tree at the stated locations.
**Evidence:**
> MmsValue_decodeMmsData — src/mms/iso_mms/server/mms_access_result.c:352-356, declared src/mms/inc/mms_value.h:1018-1019.
> MmsValue_decodeMmsDataMaxRecursion — src/mms/iso_mms/server/mms_access_result.c:346-350, declared src/mms/inc/mms_value.h:1032-1033.
> MmsValue_encodeMmsData — src/mms/iso_mms/server/mms_access_result.c:520-521 (def), declared src/mms/inc/mms_value.h:1046-1047.
> MmsValue_printToBuffer — src/mms/iso_mms/common/mms_value.c:2290-2291 (def), declared src/mms/inc/mms_value.h:1003-1004.
> MmsValue_delete — src/mms/iso_mms/common/mms_value.c:1389-1390, declared src/mms/inc/mms_value.h:808.
> MmsValue_createEmptyArray / MmsValue_createEmptyStructure — src/mms/iso_mms/common/mms_value.c:2140-2141, 2169-2170; declared src/mms/inc/mms_value.h:114, 719.
> getNumberOfElements, MmsValue_decodeMmsDataRecursive, encodeArrayAccessResult, encodeStructuredAccessResult — all `static` in src/mms/iso_mms/server/mms_access_result.c (lines 28-63, 65-92, 99-153, 155-344 respectively) — internal core, not exported/public, confirming they are "core" not "entries".
> BerDecoder_decodeLength — src/mms/asn1/ber_decode.c:126-... (def), declared src/mms/inc_private/ber_decode.h:46 (private header, not installed/public).

### P2.7 Co-call Constraints
**Claim:** There is no lock/unlock or begin/commit pairing for the decoder itself. The only paired relationship observed is create (decode) / destroy (MmsValue_delete) for the returned object, and a symmetric "size-pass then real-pass" pairing for MmsValue_encodeMmsData (`encode=false` to get required buffer size, then `encode=true` into an allocated buffer of that size) when used as the companion encoder.
**Evidence:**
> fuzz/fuzz_mms_encode.c:16-26: `int dataSize = MmsValue_encodeMmsData(mmsValue, NULL, 0, false); ... mmsBuffer = GLOBAL_MALLOC(dataSize); MmsValue_encodeMmsData(mmsValue, mmsBuffer, 0, true);` — explicit two-pass pairing.
> src/mms/iso_mms/server/mms_access_result.c:43/53 and 77/84 (encodeArrayAccessResult/encodeStructuredAccessResult): same false-pass/true-pass pairing used recursively for nested elements.

### P2.8 Prerequisite State
**Claim:** No connection, context, model, or configuration object is required before calling the target; it is a stateless decode over a caller-supplied buffer. The only "state" the caller must supply correctly is the buffer itself and consistent bufPos/bufferLength values (bufferLength must be the absolute end offset, ≥ bufPos+1, and must not exceed the real allocated size of `buffer`, since the decoder trusts bufferLength as the true bound for all internal reads such as `buffer[bufPos]` for the bitstring padding byte at line 242).
**Evidence:**
> src/mms/iso_mms/server/mms_access_result.c:163-177: function takes only buffer/bufPos/bufferLength/endBufPos/depth/maxDepth, no struct/handle/context parameter.
> src/mms/iso_mms/server/mms_access_result.c:242 (`int padding = buffer[bufPos];`) reads directly from the caller buffer trusting prior length checks — caller must ensure buffer is allocated at least bufferLength bytes.

### P2.9 Result Usage
**Claim:** The returned `MmsValue*` tree is consumed by (a) `MmsValue_encodeMmsData` to round-trip re-serialize it (the designated inverse/consumer), (b) `MmsValue_printToBuffer` to dump it to a human readable string (walks arrays/structures recursively via `MmsValue_getArraySize`/`MmsValue_getElement`), and (c) in real server/client code, by business logic that inspects data-access results or sets named variable list values, after which it is always eventually freed with `MmsValue_delete`.
**Evidence:**
> fuzz/fuzz_mms_encode.c:11-16: decoded `mmsValue` passed straight into `MmsValue_encodeMmsData(mmsValue, NULL, 0, false)`.
> fuzz/fuzz_mms_print.c:12-23: decoded `mmsValue` passed into `MmsValue_encodeMmsData(... false)` then `MmsValue_printToBuffer(mmsValue, printBuffer, kBufSize)`.
> src/mms/iso_mms/common/mms_value.c:2305-2329 (`MmsValue_printToBuffer` MMS_ARRAY/MMS_STRUCTURE case): recursively calls itself via `MmsValue_getElement(self, i)` over `MmsValue_getArraySize(self)` — i.e., it walks exactly the tree shape the decoder built (arrays/structures/components).
> src/mms/iso_mms/client/mms_client_journals.c:75-78: decoded value is stored into `journalVariable->value` (an `MmsValue*` field of a client-visible journal-entry structure) for later use by journal-reading API consumers.
> src/mms/iso_mms/server/mms_write_service.c:317-333: decoded `newValue` is immediately printed via `MmsValue_printToBuffer` (debug path) or, on NULL, triggers `mmsMsg_createMmsRejectPdu(...)` to reject the PDU.
> examples/server_example_logging/server_example_logging.c:100-108: decoded `value` passed to `MmsValue_printToBuffer` then `MmsValue_delete(value)`.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters/flags observed:
1. `maxDepth` of `MmsValue_decodeMmsDataMaxRecursion` (fixed to 25 inside `MmsValue_decodeMmsData`) bounds recursion for nested 0xa1/0xa2 tags — exceeding it makes the decoder return NULL instead of crashing/stack-overflowing.
2. The leading BER tag byte dispatches to a different decode path for each MMS type (0xa1 array, 0xa2 structure, 0x80 data-access-error, 0x83 boolean, 0x84 bit string, 0x85 integer, 0x86 unsigned, 0x87 float/double by length 5/9, 0x89 octet string, 0x8a visible string, 0x8c binary time by length 4/6, 0x90 mms string, 0x91 utc time length must be 8, 0x00 ignored indefinite-length end tag, any other value → error).
3. `dataLength==0` is rejected unless tag is 0x8a/0x90/0x89 (string/octet-string types may be legitimately empty).
4. `MmsValue_encodeMmsData`'s `encode` boolean selects length-computation-only vs. actual-byte-encoding pass.
5. `MmsValue_printToBuffer`'s `bufferSize` truncates/limits recursive output and is checked at every recursive call to avoid overflow.
**Evidence:**
> src/mms/iso_mms/server/mms_access_result.c:158-161: `depth++; if (depth > maxDepth) return NULL;`
> src/mms/iso_mms/server/mms_access_result.c:186-331: the full `switch (tag)` dispatch, e.g. lines 240-252 (0x84 bit string + padding check), 254-262 (0x85 length>8 reject), 275-281 (0x87 length 5/9), 314-324 (0x91 length must be 8), 329-330 (default → error).
> src/mms/iso_mms/server/mms_access_result.c:180-184: `if (tag != 0x8a && tag != 0x90 && tag != 0x89 && dataLength == 0) goto exit_with_error;`
> src/mms/iso_mms/server/mms_access_result.c:526-620 (`MmsValue_encodeMmsData` switch on `encode`): every case branches on the `bool encode` parameter between size-only and real-encode.
> src/mms/iso_mms/common/mms_value.c:2300-2324: `if (bufferSize == 0) break;` and `bufPos += strnlen(...); if (bufPos >= bufferSize) break;` — bufferSize bounds recursive writes.

### P2.11 Input Expectations
**Claim:** Input is expected to be a single BER tag/length/value-encoded "MMS Data" element. Minimum size ≥1 byte (`bufferLength < 1` rejected). Length can be short or long BER form, decoded by `BerDecoder_decodeLength`, which itself rejects length fields pointing past `maxBufPos`, more than 3 length-size bytes, or negative lengths; indefinite length (0x80 lenLength) is handled via `getIndefiniteLength` with its own depth bound. Per-tag size constraints: bit string padding byte must be 0-7; integer/unsigned length ≤8 bytes; float/double length must be exactly 5 or 9; binary time length must be 4 or 6; UTC time length must be exactly 8; data length must be >0 except for octet-string/visible-string/mms-string tags, which may be 0 (empty). No other framing/terminator is required beyond the single TLV; nested arrays/structures recurse through the same grammar for each child, with sibling counts calculated by `getNumberOfElements` which independently re-walks the same tag space and returns -1 on any malformed encoding (any unknown tag anywhere in the tree is a hard parse error).
**Evidence:**
> src/mms/iso_mms/server/mms_access_result.c:165-168: `int dataEndBufPos = bufferLength; if (bufferLength < 1) goto exit_with_error;`
> src/mms/asn1/ber_decode.c:81-124 (`BerDecoder_decodeLengthRecursive`): short form (`len1` plain byte), long form (up to 3 length bytes, `lenLength < 4` else reject), bound checks `bufPos + (*length) > maxBufPos`.
> src/mms/asn1/ber_decode.c:31-71 (`getIndefiniteLength`): indefinite-length (0x80) handling with its own `depth`/`maxDepth` recursion guard and 00 00 terminator scan.
> src/mms/iso_mms/server/mms_access_result.c:182-184, 244-245, 255-256, 265-266, 276-281 (2 branches implying reject otherwise, confirmed by lack of else in 275-281 leaving value NULL->later treated as error by caller if size not matched), 295-305, 315-322: all the per-tag numeric constraints.
> src/mms/iso_mms/server/mms_access_result.c:99-153 (`getNumberOfElements`): re-walks tag space to count siblings, `default: goto exit_with_error;` at line 141 returns -1 for unknown tags, propagated at line 193 (`if (elementCount < 0) goto exit_with_error;`).

---

## Other entries: call protocol and combination with the target

### Entry: MmsValue_decodeMmsDataMaxRecursion
**Claim:** Same signature/semantics as `MmsValue_decodeMmsData` plus an explicit `int maxDepth` parameter controlling nested array/structure recursion depth (the target hardcodes `maxDepth=25`). No init needed before it; same parameter rules as the target (bufferLength is absolute end offset, endBufPos optional). Return value: `MmsValue*` or NULL on error, same semantics as the target. Freed the same way via `MmsValue_delete`. In a harness it can be used as an alternative/companion to the target to let the fuzzer vary recursion depth directly (e.g., deriving `maxDepth` from a fuzzer-controlled byte) instead of using the fixed 25 depth of `MmsValue_decodeMmsData`.
**Evidence:**
> src/mms/inc/mms_value.h:1021-1033 (doc + decl): "with a defined maximum recursion depth ... \param maxDepth the maximum recursion depth".
> src/mms/iso_mms/server/mms_access_result.c:346-350: `MmsValue_decodeMmsDataMaxRecursion(...) { return MmsValue_decodeMmsDataRecursive(buffer, bufPos, bufferLength, endBufPos, 0, maxDepth); }` — depth starts at 0, same underlying core as the target.
> src/mms/iso_mms/server/mms_access_result.c:352-356: the target itself is just this function called with `maxDepth=25`, confirming they share identical decode behavior/parameter contract except for depth.

### Entry: MmsValue_encodeMmsData
**Claim:** Must be called with a non-NULL `MmsValue* self` that is either produced by the target (decode) or constructed via other `MmsValue_new*`/`MmsValue_createEmpty*` APIs; it is **not** safe to call on a NULL `self` (dereferences `self->type` unconditionally at line 525 with no NULL check). Call protocol in a round-trip harness: (1) call with `buffer=NULL, encode=false` to get the required `size` (returns `int`, 0 or the size); (2) if `size>0`, allocate a buffer of that size; (3) call again with `buffer=<allocated>, bufPos=0, encode=true` to actually serialize. Parameter `bufPos` is the write cursor into `buffer` (0 for top-level calls; nonzero for appending after a BER tag/length prefix as seen in `encodeArrayAccessResult`/`encodeStructuredAccessResult`). Return value on the `encode=true` path is the new bufPos after writing (not a status code) — callers must not treat it as success/failure, just as the next write offset. Cleanup: the harness owns the allocated `buffer` and must free it (e.g. `GLOBAL_FREEMEM`); the `self` (`MmsValue*`) is a separate allocation freed independently via `MmsValue_delete`.
**Evidence:**
> src/mms/iso_mms/server/mms_access_result.c:521-525: `int MmsValue_encodeMmsData(MmsValue* self, uint8_t* buffer, int bufPos, bool encode) { int size = 0; switch (self->type) {...` — no NULL check on `self`.
> src/mms/inc/mms_value.h:1040-1047 (doc): "\param encode encode to buffer (true) or calculate length only (false)" / "\return the encoded length of the corresponding MMS data element".
> fuzz/fuzz_mms_encode.c:16-28: exact two-call pattern (`false` then `true`) with `GLOBAL_MALLOC`/`GLOBAL_FREEMEM` around it, independent of `MmsValue_delete(mmsValue)` at line 31.
> src/mms/iso_mms/server/mms_access_result.c:43,53 and 77,84: recursive self-calls inside `encodeArrayAccessResult`/`encodeStructuredAccessResult` follow the identical false-pass/true-pass convention, confirming it's the standard protocol.

### Entry: MmsValue_printToBuffer
**Claim:** Takes a (possibly NULL — explicitly handled) `const MmsValue* self`, a caller-allocated `char* buffer`, and `int bufferSize`. No init required; typically called directly on the target's decode result. It does not allocate or free anything — it writes into the caller's buffer and returns a pointer to the same buffer (`const char*` return equals `buffer`), truncating output if `bufferSize` is exceeded. It must be called after a successful (non-NULL) decode for meaningful output, but is NULL-safe (prints "(null)") so it can safely be the only consumer in a harness that doesn't branch on decode success. No cleanup beyond the caller's own stack/heap buffer, independent from `MmsValue_delete(self)` which the harness must still call afterward to free the decoded tree.
**Evidence:**
> src/mms/iso_mms/common/mms_value.c:2290-2301: `if (self == NULL) { StringUtils_copyStringMax(buffer, bufferSize, "(null)"); return buffer; } if (bufferSize) buffer[0] = 0;`
> src/mms/inc/mms_value.h:991-1004 (doc): "buffer the buffer where to copy the string representation" / "bufferSize the size of the provided buffer" / "\return a pointer to the start of the buffer" — confirms no ownership transfer.
> fuzz/fuzz_mms_print.c:22-23: `char printBuffer[kBufSize]; MmsValue_printToBuffer(mmsValue, printBuffer, kBufSize);` — stack buffer, no free needed for the print buffer itself; `MmsValue_delete(mmsValue)` still called separately at line 26.
> examples/server_example_logging/server_example_logging.c:102-108: `char buffer[256]; MmsValue_printToBuffer(value, buffer, 256); ...; MmsValue_delete(value);` — same pattern in a real (non-fuzz) consumer.

### How the entries combine in one harness
**Claim:** The project's own `fuzz/` directory already demonstrates the canonical combination: `MmsValue_decodeMmsData(data, 0, size, &out)` → branch on NULL → `MmsValue_encodeMmsData(mmsValue, NULL, 0, false)` (sanity/size probe, also branch on `<=0`) → either `MmsValue_printToBuffer` (fuzz_mms_print.c) or a full `encode=true` pass into a freshly malloc'd buffer (fuzz_mms_encode.c) → `MmsValue_delete(mmsValue)` at the end in all cases. `MmsValue_decodeMmsDataMaxRecursion` is not used by any in-tree fuzzer but is the natural depth-controlled sibling of the target and can replace it 1:1 with an extra fuzzed `maxDepth` argument.
**Evidence:**
> fuzz/fuzz_mms_encode.c:1-35 (whole file): decode → encode(false) → malloc → encode(true) → free(buffer) → MmsValue_delete.
> fuzz/fuzz_mms_print.c:1-30 (whole file): decode → encode(false) → printToBuffer → MmsValue_delete.
> fuzz/fuzz_mms_decode.c:1-16: current stub does not call the target with real arguments at all (symbol-keep-alive only) — confirms this file is not a template for correct usage; fuzz_mms_encode.c / fuzz_mms_print.c are the reliable references.

### Build notes
**Claim:** Fuzzers are plain C, compiled against the static libs `src/libiec61850.a` and `hal/libhal.a` built by the project's own CMake; the target's header is reached indirectly through the umbrella header `iec61850_server.h` (which includes `mms_server.h` → ... → `mms_value.h`), with include dirs auto-discovered from `../src` and `../hal`. `GLOBAL_MALLOC`/`GLOBAL_FREEMEM` (from `lib_memory.h`) are the project's allocator wrappers used for buffers passed to `MmsValue_encodeMmsData`.
**Evidence:**
> ossfuzz_view/libiec61850-c1/build.sh:18-24: `cd libiec61850; mkdir build && cd build; cmake ../; make` then `INC="$(find ../src ../hal -name '*.h' ... -printf '-I%h\n' ...)"` — all headers under src/ and hal/ are put on the include path, so `mms_value.h` (src/mms/inc/mms_value.h) is reachable directly or via `iec61850_server.h`.
> ossfuzz_view/libiec61850-c1/build.sh:27-31: `$CC $CFLAGS -include stdbool.h $LIB_FUZZING_ENGINE "$src" -c -o "${fuzzer}.o" $INC` then linked with `./src/libiec61850.a ./hal/libhal.a` — plain C compilation (`$CC`, not `$CXX`, for the .c→.o step; `$CXX` only used for final link with the fuzzing engine).
> fuzz/fuzz_mms_encode.c:4-6: `#include "iec61850_server.h"`, `#include "hal_thread.h"`, `#include "lib_memory.h"` — confirms header include path used by real harnesses, and `lib_memory.h` for `GLOBAL_MALLOC`/`GLOBAL_FREEMEM`.
> src/iec61850/inc/iec61850_server.h:43: `#include "mms_server.h"` — umbrella path from the public server header down toward `mms_value.h`.
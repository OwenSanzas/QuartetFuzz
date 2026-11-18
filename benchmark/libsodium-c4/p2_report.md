## target: crypto_secretbox_open_easy

### P2.1 Init Sequence
**Claim:** The libsodium process-wide initializer `sodium_init()` must be called once before any crypto_secretbox_* call (including the target). No secretbox-specific "init" object exists; the function is stateless and works directly on caller-provided buffers/key/nonce.
**Evidence:**
> ossfuzz_view/libsodium-c4/crypto_box_fuzzer.cc:22: `int initialized = sodium_init(); assert(initialized >= 0);` (shown as the project's own fuzzer pattern for a sibling API in the same library)
> ossfuzz_view/libsodium-c4/fake_random.h:61: `int initialized = sodium_init();` called inside `setup_fake_random()`, which other fuzzers in this OSS-Fuzz target call before any crypto_* use
> test/default/secretbox_easy.c (same family, uses `cmptest.h`) — `cmptest.h` calls `sodium_init()` at line 146/185/223 of the harness-generation helper before invoking test `main()`s that call crypto_secretbox_* functions (test/default/cmptest.h:146)
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:16-148: crypto_secretbox_open_easy/open_detached/easy/detached/keygen do not call sodium_init() themselves — they assume global library state (e.g. selected implementations for core_hsalsa20/stream_salsa20/onetimeauth_poly1305) is already initialized.

### P2.2 Parameter Construction
**Claim:** `m` is the output plaintext buffer, must be writable and at least `clen - crypto_secretbox_MACBYTES` bytes; `c` is the input ciphertext (first `crypto_secretbox_MACBYTES` bytes are the Poly1305 MAC, non-null, `clen` bytes); `clen` is `unsigned long long`, must be `>= crypto_secretbox_MACBYTES` (16U) or the call fails; `n` is a `crypto_secretbox_NONCEBYTES` (24-byte) buffer, non-null; `k` is a `crypto_secretbox_KEYBYTES` (32-byte) buffer, non-null. `m` is explicitly *not* in the nonnull list so NULL is accepted (verify-only mode).
**Evidence:**
> src/libsodium/include/sodium/crypto_secretbox.h:41-45: `int crypto_secretbox_open_easy(unsigned char *m, const unsigned char *c, unsigned long long clen, const unsigned char *n, const unsigned char *k) __attribute__((warn_unused_result)) __attribute__((nonnull(2, 4, 5)));` — note nonnull covers c(2), n(4), k(5) but not m(1).
> src/libsodium/include/sodium/crypto_secretbox.h:16,20,24: `crypto_secretbox_KEYBYTES` = `crypto_secretbox_xsalsa20poly1305_KEYBYTES`; `crypto_secretbox_NONCEBYTES` = `..._NONCEBYTES`; `crypto_secretbox_MACBYTES` = `..._MACBYTES`.
> src/libsodium/include/sodium/crypto_secretbox_xsalsa20poly1305.h:15,19,23: `#define crypto_secretbox_xsalsa20poly1305_KEYBYTES 32U`, `..._NONCEBYTES 24U`, `..._MACBYTES 16U`.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:142-144: `if (clen < crypto_secretbox_MACBYTES) { return -1; }` — explicit lower-bound check on clen before dispatch.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:101-105 (inside crypto_secretbox_open_detached): `if (m == NULL) { sodium_memzero(subkey, ...); sodium_memzero(block0, ...); return 0; }` — target tolerates `m == NULL` for "verify only" use.

### P2.3 Object Lifecycle
**Claim:** There is no opaque object/handle; lifecycle is simply: allocate buffers for `m`,`c`(or mac+c),`n`,`k` → (optionally) fill `k` via `crypto_secretbox_keygen` and produce `c` via `crypto_secretbox_easy`/`crypto_secretbox_detached` → call `crypto_secretbox_open_easy` (or `crypto_secretbox_open_detached`) → consume `m` → zero/free buffers.
**Evidence:**
> test/default/secretbox_easy2.c:18-23: allocates m, m2, c, nonce, k, mac with `sodium_malloc`; fills k via `crypto_secretbox_keygen(k)` (line 24); fills m, nonce via `randombytes_buf`.
> test/default/secretbox_easy2.c:27-33: `crypto_secretbox_easy(c, m, mlen, nonce, k); ... crypto_secretbox_open_easy(m2, c, mlen + MACBYTES, nonce, k)`.
> test/default/secretbox_easy2.c:64-69: `sodium_free(m); sodium_free(m2); sodium_free(c); sodium_free(nonce); sodium_free(k); sodium_free(mac);` — explicit cleanup of every heap buffer, order not significant (independent allocations).

### P2.4 Return Value Handling
**Claim:** Return 0 = authentication succeeded and (if `m != NULL`) `m` now holds decrypted plaintext; return -1 = authentication failed (tampered ciphertext/MAC, wrong key/nonce, or `clen < MACBYTES`) and `m` must be treated as **not populated/untrusted** (on the MAC-fail path `open_detached` zeroes subkey/block0 and returns -1 before ever touching `m`). The function is marked `warn_unused_result`; callers must check the return value, not just use `m`.
**Evidence:**
> src/libsodium/include/sodium/crypto_secretbox.h:41-45: `__attribute__ ((warn_unused_result))`.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:97-100 (open_detached): `if (crypto_onetimeauth_poly1305_verify(mac, c, clen, block0) != 0) { sodium_memzero(subkey, sizeof subkey); return -1; }` — on failure, m is never written.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:142-147 (open_easy): `if (clen < crypto_secretbox_MACBYTES) { return -1; } return crypto_secretbox_open_detached(...);`
> test/default/secretbox_easy.c:88-91,96-100: checks return value against 0 (success) and -1 (tampered ciphertext must return -1): `if (crypto_secretbox_open_easy(c, c, crypto_secretbox_MACBYTES, nonce, firstkey) != -1) { printf("Null tampered crypto_secretbox_open_easy() failed\n"); }`
> test/default/secretbox_easy2.c:35-41: iterates over short `clen` values and asserts `crypto_secretbox_open_easy(...) == 0` must NOT happen (`"short open() should have failed"`), confirming callers must branch on the return code.

### P2.5 Cleanup Sequence
**Claim:** No library-managed resources are allocated by the target itself; cleanup is only of caller-owned buffers (m, c, n, k, mac) with `sodium_free`/`free`/stack unwind, independent of order. Internally the function zeroes its own stack secrets (`subkey`, `block0`, poly1305 `state`) via `sodium_memzero` on both success and failure paths — this is automatic, not something the caller does.
**Evidence:**
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:98,102-103,127,132: multiple `sodium_memzero(subkey, ...)` / `sodium_memzero(block0, ...)` calls on every return path of crypto_secretbox_open_detached.
> test/default/secretbox_easy2.c:64-69: caller-side `sodium_free()` for every `sodium_malloc`-ed buffer after all crypto_secretbox_* calls are done.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the given paths.
**Evidence:**
> src/libsodium/include/sodium/crypto_secretbox.h:37 crypto_secretbox_easy
> src/libsodium/include/sodium/crypto_secretbox.h:42 crypto_secretbox_open_easy
> src/libsodium/include/sodium/crypto_secretbox.h:48 crypto_secretbox_detached
> src/libsodium/include/sodium/crypto_secretbox.h:56 crypto_secretbox_open_detached
> src/libsodium/include/sodium/crypto_secretbox.h:65 crypto_secretbox_keygen
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:16,70,82,137 — definitions of detached, easy, open_detached, open_easy
> src/libsodium/crypto_secretbox/crypto_secretbox.c:63-67 — definition of crypto_secretbox_keygen

### P2.7 Co-call Constraints
**Claim:** `crypto_secretbox_easy`/`crypto_secretbox_detached` (encrypt) and `crypto_secretbox_open_easy`/`crypto_secretbox_open_detached` (decrypt) are the natural encrypt/decrypt pair sharing the same `(n,k)`; the "easy" and "detached" variants are alternate framings of the same core, not meant to be mixed arbitrarily, but detached-produced `(c, mac)` can be opened by either: by concatenating `mac||c` and calling `open_easy`, or directly via `open_detached(m, c, mac, mlen, n, k)`. No explicit lock/unlock or begin/commit pairing exists beyond "whatever encrypted it must be decrypted with the exact same `n` and `k`".
**Evidence:**
> test/default/secretbox_easy2.c:42-50: produces `(c, mac)` via `crypto_secretbox_detached(c, mac, m, mlen, nonce, k)` then consumes both via `crypto_secretbox_open_detached(m2, c, mac, mlen, nonce, k)` — same `nonce,k` pair reused for encrypt+decrypt.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:78-80 (crypto_secretbox_easy): `return crypto_secretbox_detached(c + crypto_secretbox_MACBYTES, c, m, mlen, n, k);` — shows `easy` ciphertext layout is `mac (MACBYTES) || ciphertext`, matching what `open_easy` expects to split at line 145: `crypto_secretbox_open_detached(m, c + crypto_secretbox_MACBYTES, c, clen - crypto_secretbox_MACBYTES, n, k)`.

### P2.8 Prerequisite State
**Claim:** No connection/context object is required. The only prerequisite "state" is: (a) library initialized via `sodium_init()` (selects CPU-specific implementations of hsalsa20/salsa20/poly1305), and (b) caller already possesses a valid 32-byte key and 24-byte nonce, and a ciphertext produced under that exact (key, nonce) pair (or attacker-supplied bytes to test forgery rejection). There is no persistent handle carried between calls.
**Evidence:**
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:94 (open_detached): `crypto_core_hsalsa20(subkey, n, k, NULL);` — nonce/key are consumed fresh each call, no cached state.
> ossfuzz_view/libsodium-c4/fake_random.h:61-62: pattern shows sodium_init() being the only global prerequisite before any secretbox-family call in this OSS-Fuzz project.

### P2.9 Result Usage
**Claim:** The decrypted plaintext in `m` (when return==0) is used directly by the caller for comparison/consumption; in the test-suite it's compared with `memcmp` against the known original. There is no further "parsing API" in libsodium itself that consumes `m` — libsodium only guarantees the raw decrypted bytes are in `m`, and it is the caller's responsibility to process them.
**Evidence:**
> test/default/secretbox_easy2.c:33: `printf("%d\n", memcmp(m, m2, mlen));` immediately after `crypto_secretbox_open_easy(m2, c, ..., nonce, k)` — `m2` (the `m` output) consumed by memcmp.
> test/default/secretbox_easy2.c:57-62: a second open_easy call writes back in place (`c` used as both input and `m` output), result compared again with `memcmp(m, c, mlen)` — demonstrates in-place (aliased) decryption is a supported/consumed usage.
> crypto_box_fuzzer.cc:51-53 (sibling API, same pattern applies to secretbox): `err = crypto_box_open_easy(decrypted, ...); assert(err == 0); assert(memcmp(decrypted, msg, msg_len) == 0);` — decrypted buffer consumed via comparison only, no further libsodium API call on it.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting inputs are: (1) `clen` value — `< MACBYTES` ⇒ immediate failure in `open_easy`; `<=64-ZEROBYTES+MACBYTES`(i.e. first-block-only) vs larger ⇒ selects whether only `crypto_stream_salsa20_xor` is used or also `crypto_stream_salsa20_xor_ic` for the remainder (block-boundary path); `clen==MACBYTES` ⇒ zero-length plaintext ("null message", still verified); (2) `m == NULL` ⇒ verify-only mode (decrypt skipped) only reachable via `open_detached`, not via `open_easy` directly since `open_easy` always passes a non-NULL-checked `m` from caller — but caller of `open_easy` could still pass NULL since it's not in the `nonnull` list, triggering the verify-only path; (3) buffer aliasing of `m`/`c` (and `c`/`m` in the encrypt direction) ⇒ selects the `memmove`-based overlap-safe branch vs the straight no-overlap branch; (4) tampering with the first `MACBYTES` of `c` (the embedded MAC) vs tampering with ciphertext body ⇒ both cause Poly1305 verification failure (return -1) but exercise different code the MAC check covers (`crypto_onetimeauth_poly1305_verify(mac, c, clen, block0)` covers the whole `c` of length `clen`, i.e. body included).
**Evidence:**
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:142-144: `clen < crypto_secretbox_MACBYTES` branch.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:114-131: `mlen0 = clen; if (mlen0 > 64U - crypto_secretbox_ZEROBYTES) { mlen0 = 64U - crypto_secretbox_ZEROBYTES; } ... if (clen > mlen0) { crypto_stream_salsa20_xor_ic(m + mlen0, c + mlen0, clen - mlen0, n + 16, 1U, subkey); }` — block-boundary dispatch.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:101-105: `m == NULL` verify-only branch (only in `open_detached`, reachable from `open_easy` if caller passes NULL m since `nonnull` excludes param 1).
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:107-113: overlap-detection `memmove` branch based on pointer arithmetic between `c` and `m`.
> test/default/secretbox_easy.c:96-100: `c[randombytes_uniform(crypto_secretbox_MACBYTES)]++;` then expects `open_easy(...) != -1` to be the failure case — demonstrates MAC-byte tampering path.

### P2.11 Input Expectations
**Claim:** `c` has no format/header beyond the implicit layout "first `crypto_secretbox_MACBYTES` (16) bytes = Poly1305 tag, remaining `clen - MACBYTES` bytes = Salsa20-encrypted ciphertext body"; there is no length field, no terminator — `clen` (explicit parameter) is authoritative. Minimum valid `clen` is `MACBYTES` (16), representing an empty plaintext. Maximum plaintext size is bounded by `crypto_secretbox_MESSAGEBYTES_MAX` (checked only in `crypto_secretbox_easy`'s encrypt direction via `sodium_misuse()`, not re-checked in `open_easy`/`open_detached`). `n` must be exactly `crypto_secretbox_NONCEBYTES` (24) bytes and `k` exactly `crypto_secretbox_KEYBYTES` (32) bytes — these are fixed-size fields, not length-prefixed, so the harness must supply exactly that many bytes.
**Evidence:**
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:142-147: layout split `c + crypto_secretbox_MACBYTES` as ciphertext body, `c` itself (first 16 bytes only used) as mac.
> src/libsodium/include/sodium/crypto_secretbox.h:24: `crypto_secretbox_MACBYTES` = 16 (via xsalsa20poly1305 header, `crypto_secretbox_xsalsa20poly1305.h:23`).
> src/libsodium/include/sodium/crypto_secretbox.h:16,20: KEYBYTES=32, NONCEBYTES=24 (xsalsa20poly1305.h:15,19).
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:75-77 (crypto_secretbox_easy only): `if (mlen > crypto_secretbox_MESSAGEBYTES_MAX) { sodium_misuse(); }` — this ceiling check exists only on the encrypt path, not duplicated in open_easy/open_detached (clen is taken as given, limited only by `unsigned long long` range and MACBYTES lower bound).

## Other entries in the feature (call protocol & how they combine with the target)

### crypto_secretbox_easy (encrypt, inverse of target)
**Claim:** Needs `sodium_init()` done globally; needs caller-allocated `c` buffer sized `mlen + crypto_secretbox_MACBYTES`, a message `m` (may be 0 length), `mlen` (<= `crypto_secretbox_MESSAGEBYTES_MAX`, else `sodium_misuse()` abort/panic), a 24-byte nonce `n`, and a 32-byte key `k`. Parameters `c,n,k` are `nonnull` (c=1,n=4,k=5); `m` is NOT required nonnull by the attribute list but is dereferenced unless `mlen==0` is special-cased internally (loop bounded by `mlen0`, safe for `mlen==0`). Returns `int`, always 0 on success per current implementation (no error path reachable besides the `sodium_misuse()` abort on oversized `mlen`). Produces ciphertext `c = mac(16B) || encrypted(mlen B)`, which the harness feeds straight into `crypto_secretbox_open_easy(m2, c, mlen+MACBYTES, n, k)` using the *same* `n,k`. Nothing to "free" beyond the caller's own buffer.
**Evidence:**
> src/libsodium/include/sodium/crypto_secretbox.h:36-39: declaration with `nonnull(1,4,5)` (no `warn_unused_result`, consistent with always-returns-0 behavior).
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:70-80: body, calls `crypto_secretbox_detached(c + MACBYTES, c, m, mlen, n, k)` after the `MESSAGEBYTES_MAX` check.
> test/default/secretbox_easy2.c:20,27: `c = sodium_malloc(crypto_secretbox_MACBYTES + mlen); ... crypto_secretbox_easy(c, m, mlen, nonce, k);` then immediately fed to `crypto_secretbox_open_easy` at line 28-30 with the same `nonce, k`.

### crypto_secretbox_detached (encrypt, separate-mac variant)
**Claim:** Same prerequisites as `crypto_secretbox_easy` but caller supplies two separate output buffers: `c` (ciphertext body, `mlen` bytes) and `mac` (`crypto_secretbox_MACBYTES` = 16 bytes), avoiding the "mac must be adjacent to ciphertext" layout. Declared `nonnull(1,2,5,6)` i.e. `c,mac,n,k` required, `m` optional-looking but dereferenced for `mlen>0`. Always returns 0 (int return, no `warn_unused_result`, no observed failure path in current source). Its outputs `(c, mac)` feed `crypto_secretbox_open_detached(m, c, mac, mlen, n, k)` directly, or can be concatenated (`mac || c`) and fed to `crypto_secretbox_open_easy`.
**Evidence:**
> src/libsodium/include/sodium/crypto_secretbox.h:47-53: declaration, `nonnull(1,2,5,6)`.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:16-68: definition; produces ciphertext in `c` and tag in `mac` via Poly1305 `init/update/final` (lines 51,63-64).
> test/default/secretbox_easy2.c:42-50: `crypto_secretbox_detached(c, mac, m, mlen, nonce, k); ... crypto_secretbox_open_detached(m2, c, mac, mlen, nonce, k)` — direct detached encrypt→decrypt pairing, no need to concatenate mac and c.

### crypto_secretbox_open_detached (decrypt, separate-mac variant; also the function the target delegates to)
**Claim:** Same key/nonce prerequisites as the target. Takes explicit `mac` (16 bytes) separately from `c` (ciphertext body only, `clen` bytes — note: here `clen` means body length, NOT body+mac as in `open_easy`). `nonnull(2,3,5,6)` i.e. `c, mac, n, k` required; `m` may be NULL for verify-only mode (checked explicitly at line 101-105: `if (m == NULL) {... return 0;}` after successful verification). `warn_unused_result`; returns 0 on success/-1 on MAC mismatch exactly like the target (because the target is a thin wrapper around it). Harness value: lets fuzz input directly control `clen` including `clen == 0` (an edge the target's own `clen - MACBYTES` subtraction can't reach as easily since `open_easy` enforces `clen>=MACBYTES` first) and lets the harness test tampering with `mac` independently of tampering with `c`.
**Evidence:**
> src/libsodium/include/sodium/crypto_secretbox.h:55-62: declaration, `warn_unused_result`, `nonnull(2,3,5,6)`.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:82-135: definition; line 97 `if (crypto_onetimeauth_poly1305_verify(mac, c, clen, block0) != 0) {...; return -1;}`; line 101-105 NULL-m verify-only branch.
> test/default/secretbox_easy2.c:43-46: `crypto_secretbox_open_detached(NULL, c, mac, mlen, nonce, k) != 0` used explicitly to test the NULL-m (verify-only) path, printing a failure message if it doesn't return 0.
> src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:137-148 (target): `return crypto_secretbox_open_detached(m, c + crypto_secretbox_MACBYTES, c, clen - crypto_secretbox_MACBYTES, n, k);` — confirms target is a wrapper.

### crypto_secretbox_keygen (configuration/lifecycle helper, not encrypt/decrypt)
**Claim:** Requires only `sodium_init()` done globally (for randombytes backend selection) and a caller-allocated 32-byte (`crypto_secretbox_KEYBYTES`) buffer `k`, declared `nonnull`. Void return (no error reporting — always "succeeds" by filling `k` with `randombytes_buf`). Must be called before any encrypt/decrypt call that needs a fresh key; used purely to produce a well-formed key `k` for `crypto_secretbox_easy`/`_detached` and matching `crypto_secretbox_open_easy`/`_open_detached` calls. Nothing to free beyond the caller's own buffer. In a fuzz harness this can be swapped for fuzz-input-derived raw 32 bytes if the harness wants the key itself to be attacker controlled (the target makes no assumptions about key provenance, it is just 32 bytes consumed by `crypto_core_hsalsa20`).
**Evidence:**
> src/libsodium/include/sodium/crypto_secretbox.h:64-66: `void crypto_secretbox_keygen(unsigned char k[crypto_secretbox_KEYBYTES]) __attribute__((nonnull));`
> src/libsodium/crypto_secretbox/crypto_secretbox.c:63-67: `void crypto_secretbox_keygen(unsigned char k[crypto_secretbox_KEYBYTES]) { randombytes_buf(k, crypto_secretbox_KEYBYTES); }`
> test/default/secretbox_easy2.c:22,24: `k = sodium_malloc(crypto_secretbox_KEYBYTES); ... crypto_secretbox_keygen(k);` immediately before use in `crypto_secretbox_easy`/`crypto_secretbox_open_easy`.
> test/default/keygen.c:29: `{ crypto_secretbox_keygen, crypto_secretbox_KEYBYTES }` — part of a generic keygen smoke-test table, confirming the function's only observable effect is filling the buffer.

## Build notes
**Claim:** Fuzzers for this project are C++ (compiled with `$CXX`, `-std=c++11`), include libsodium's public headers via `-I"$SRC/libsodium/src/libsodium/include"` (so `#include <sodium.h>` picks up the installed-style header `crypto_secretbox.h`), and are statically linked against the just-built `libsodium.a`, plus the OSS-Fuzz `$LIB_FUZZING_ENGINE`. There is already an existing OSS-Fuzz source `secretbox_easy_fuzzer.cc` in this target's directory, but it is currently a "gold harness replaced by an empty stub" placeholder that only keeps `crypto_secretbox_open_easy` linked via an `__asm__` alias trick and does not call it — i.e. no real reference call site is available there and a new harness body must be written using the entries described above.
**Evidence:**
> ossfuzz_view/libsodium-c4/build.sh:19-20: `./configure --enable-static LDFLAGS="$CXXFLAGS"` then `make -j$(nproc) all`.
> ossfuzz_view/libsodium-c4/build.sh:23-28: `for f in $SRC/*_fuzzer.cc; do ... $CXX $CXXFLAGS -std=c++11 -I"$SRC/libsodium/src/libsodium/include" "$f" -o "$OUT/${fuzzer}_fuzzer" "$SRC/libsodium/src/libsodium/.libs/libsodium.a" $LIB_FUZZING_ENGINE; done`.
> ossfuzz_view/libsodium-c4/Dockerfile:17-21: `FROM gcr.io/oss-fuzz-base/base-builder`; clones `libsodium` `stable` branch into `$SRC/libsodium`; copies `*.cc *.h` fuzzer sources into `$SRC`.
> ossfuzz_view/libsodium-c4/secretbox_easy_fuzzer.cc:1,10-16: `/* Gold harness replaced by an empty stub for baseline generation. */ ... QF_EXTERN void qf_target_0(void) __asm__("crypto_secretbox_open_easy"); ... LLVMFuzzerTestOneInput(...) { (void)data; (void)size; return 0; }` — confirms current stub does not exercise the target; other sibling fuzzers (`crypto_box_fuzzer.cc`, `crypto_aead_fuzzer.cc`) in the same directory show the real pattern: `#include <sodium.h>`, call `sodium_init()`/`setup_fake_random()`, then call the easy-mode encrypt/decrypt pair directly.
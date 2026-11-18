## target: crypto_box_open_easy

### P2.1 Init Sequence
**Claim:** No explicit `crypto_box_*` initialization object is required before calling `crypto_box_open_easy`; it is a stateless, one-shot function. The only library-wide prerequisite is that `sodium_init()` has been called (as with any libsodium API use), though `crypto_box_open_easy` itself calls no internal init guard directly (none of the functions in `src/libsodium/crypto_box/*.c` call `sodium_init`). A matching public/secret keypair (`pk`,`sk`) must already exist (produced by `crypto_box_keypair`/`crypto_box_seed_keypair`), and a ciphertext+nonce must already exist (produced by `crypto_box_easy`/`crypto_box_detached`/`crypto_box_seal` or by any attacker data in a fuzz context).
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:104-115`: `crypto_box_open_easy` body does not call sodium_init.
> `test/default/box_easy2.c:45-54`: real caller flow: `crypto_box_keypair(alicepk,alicesk); crypto_box_keypair(bobpk,bobsk); ... crypto_box_easy(c,m,mlen,nonce,bobpk,alicesk); ... crypto_box_open_easy(m2,c,mlen+crypto_box_MACBYTES,nonce,alicepk,bobsk)`.
> `ossfuzz_view/libsodium-c2/secretbox_easy_fuzzer.cc:22` (sibling easy-box-family fuzzer, same project convention): `int initialized = sodium_init(); assert(initialized >= 0);` called first, before any crypto_* API.

### P2.2 Parameter Construction
**Claim:** `m` (output) must be a writable buffer of at least `clen - crypto_box_MACBYTES` bytes (undefined if `clen < crypto_box_MACBYTES`, see P2.11); `c` must be a readable buffer of exactly `clen` bytes; `clen` is `unsigned long long` and may be attacker controlled but must be checked against `crypto_box_MACBYTES` (16) and should not exceed `crypto_box_MESSAGEBYTES_MAX + crypto_box_MACBYTES` in a well-formed scenario; `n` must point to `crypto_box_NONCEBYTES` (24) bytes; `pk` must point to `crypto_box_PUBLICKEYBYTES` (32) bytes; `sk` must point to `crypto_box_SECRETKEYBYTES` (32) bytes. `c`,`n`,`pk`,`sk` are declared `nonnull` (positions 2,4,5,6); `m` may legitimately be NULL only through the lower-level `crypto_secretbox_open_detached` zero-length-message path, but `crypto_box_open_easy`'s own prototype has no nonnull on `m` (position 1) — however `m` is written to, so passing NULL with `clen > crypto_box_MACBYTES` will crash.
**Evidence:**
> `src/libsodium/include/sodium/crypto_box.h:66-70`: `int crypto_box_open_easy(unsigned char *m, const unsigned char *c, unsigned long long clen, const unsigned char *n, const unsigned char *pk, const unsigned char *sk) __attribute__((nonnull(2,4,5,6)));`
> `src/libsodium/include/sodium/crypto_box_curve25519xsalsa20poly1305.h:19-35`: `PUBLICKEYBYTES 32U`, `SECRETKEYBYTES 32U`, `NONCEBYTES 24U`, `MACBYTES 16U`.
> `src/libsodium/crypto_box/crypto_box_easy.c:104-111`: `if (clen < crypto_box_MACBYTES) { return -1; } return crypto_box_open_detached(m, c + crypto_box_MACBYTES, c, clen - crypto_box_MACBYTES, n, pk, sk);` — shows c is read as MAC (first MACBYTES) + ciphertext body, m sized `clen-MACBYTES`.

### P2.3 Object Lifecycle
**Claim:** There is no persistent object/context for `crypto_box_open_easy`: it is create-key → (produce or receive ciphertext) → call once → done. The only lifecycle-bearing entities are: (1) the keypair buffers (`pk`,`sk`), created by `crypto_box_keypair`/`crypto_box_seed_keypair` and not requiring explicit destruction (plain stack/heap byte arrays, optionally `sodium_memzero`'d by the caller for hygiene — internal callees already zero their own ephemeral key material); (2) heap buffers for `m`/`c` allocated/freed by the caller (plain `malloc`/`sodium_malloc` + `free`/`sodium_free`).
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:73-89` (`crypto_box_open_detached`): allocates `unsigned char k[crypto_box_BEFORENMBYTES]` on the stack, calls `crypto_box_beforenm`, then `crypto_box_open_detached_afternm`, then `sodium_memzero(k, sizeof k);` before returning — internal key material is zeroed automatically, caller need not manage it.
> `test/default/box_seal.c:19-21,37-39`: `m = sodium_malloc(...); ... sodium_free(c); sodium_free(m); sodium_free(m2);` — shows caller-owned buffer lifecycle around calls that reach `crypto_box_open_easy` (via `crypto_box_seal_open`).

### P2.4 Return Value Handling
**Claim:** Returns `0` on success (MAC verified, plaintext written into `m`) and `-1` on any failure: `clen < crypto_box_MACBYTES` (short ciphertext), MAC verification failure (forged/corrupted ciphertext or wrong key), or a degenerate/small-order public key causing `crypto_scalarmult_curve25519` to fail. The function is `__attribute__((warn_unused_result))`, so the return value must be checked; on `-1` the content of `m` is unspecified/should not be trusted (some of `m` may have been partially written only in the `0 == ret` path per implementation, but a caller must not use `m` unless return is `0`).
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:109-111`: `if (clen < crypto_box_MACBYTES) { return -1; }`.
> `src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:97-100` (reached via `crypto_box_open_detached`→`crypto_secretbox_open_detached`): `if (crypto_onetimeauth_poly1305_verify(mac, c, clen, block0) != 0) { sodium_memzero(subkey, sizeof subkey); return -1; }` — MAC check happens before any plaintext bytes are written to `m`.
> `src/libsodium/crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c:42-44` (beforenm path, reached via `crypto_box_beforenm`): `if (crypto_scalarmult_curve25519(s, sk, pk) != 0) { return -1; }` — small-order/degenerate public key causes failure that propagates up to `crypto_box_open_easy` returning -1.
> `test/default/box_easy.c:60-69`: `ret = crypto_box_open_easy(...); assert(ret == 0); ... c[randombytes_uniform(crypto_box_MACBYTES)]++; ret = crypto_box_open_easy(...); assert(ret == -1);` — demonstrates checking the return value to detect forged ciphertext.
> `test/default/box_easy2.c:60-66`: loop that asserts every truncated `clen` value from `0` to `mlen+MACBYTES-1` returns nonzero (`!= 0`) — "short open() should have failed" if it returned 0.

### P2.5 Cleanup Sequence
**Claim:** No sodium-specific cleanup object is created by `crypto_box_open_easy` itself. On both success and error paths, the only cleanup is freeing caller-owned buffers (`m`, `c`, `pk`, `sk`, `n`) if heap-allocated, and optionally `sodium_memzero`ing sensitive buffers like `sk` and the recovered plaintext `m` when done with them. Internal ephemeral key material (`k` the shared secret, `subkey`) is already zeroed by the library internally regardless of success/failure.
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:85-89` (`crypto_box_open_detached`): `ret = crypto_box_open_detached_afternm(...); sodium_memzero(k, sizeof k); return ret;` — zeroization occurs unconditionally on both success and failure before returning.
> `test/default/box_seal.c:37-39`: `sodium_free(c); sodium_free(m); sodium_free(m2);` — caller-side cleanup of its own buffers after the open call.
> `src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:97-99`: on MAC failure, `sodium_memzero(subkey, sizeof subkey); return -1;` — internal subkey zeroed even in the failure path.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited paths: `crypto_box_open_easy`, `crypto_box_easy`, `crypto_box_detached`, `crypto_box_open_detached`, `crypto_box_detached_afternm`, `crypto_box_open_detached_afternm`, `crypto_box_easy_afternm`, `crypto_box_open_easy_afternm`, `crypto_box_beforenm`, `crypto_box_seal`, `crypto_box_seal_open`, `crypto_box_keypair`, `crypto_box_seed_keypair`, `crypto_box_curve25519xsalsa20poly1305_beforenm`, `crypto_scalarmult_curve25519`, `crypto_secretbox_detached`, `crypto_secretbox_open_detached`.
**Evidence:**
> `src/libsodium/include/sodium/crypto_box.h:51-138` — declares `crypto_box_easy`, `crypto_box_open_easy`, `crypto_box_detached`, `crypto_box_open_detached`, `crypto_box_beforenm`, `crypto_box_easy_afternm`, `crypto_box_open_easy_afternm`, `crypto_box_detached_afternm`, `crypto_box_open_detached_afternm`, `crypto_box_seal`, `crypto_box_seal_open`, `crypto_box_seed_keypair`, `crypto_box_keypair`.
> `src/libsodium/crypto_box/crypto_box_easy.c:1-115` — defines all `_easy`/`_detached`/`_afternm` variants.
> `src/libsodium/crypto_box/crypto_box.c:64-82` — defines `crypto_box_seed_keypair`, `crypto_box_keypair`, `crypto_box_beforenm` (thin wrappers to `crypto_box_curve25519xsalsa20poly1305_*`).
> `src/libsodium/crypto_box/crypto_box_seal.c:24-64` — defines `crypto_box_seal`, `crypto_box_seal_open`.
> `src/libsodium/crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c:35-49` — defines `crypto_box_curve25519xsalsa20poly1305_beforenm`.
> `src/libsodium/crypto_scalarmult/curve25519/scalarmult_curve25519.c:14-28` — defines `crypto_scalarmult_curve25519`.
> `src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:16-68,82-135` — defines `crypto_secretbox_detached`, `crypto_secretbox_open_detached`.

### P2.7 Co-call Constraints
**Claim:** The only "paired" constraint directly on `crypto_box_open_easy` is that its ciphertext `c`/nonce `n` must have been produced by a matching `crypto_box_easy`/`crypto_box_detached` call (or equivalent) using the *same nonce* and *the sender's secret key paired with the receiver's public key* (ECDH is symmetric: `crypto_box_easy(c, m, mlen, n, pk_B, sk_A)` pairs with `crypto_box_open_easy(m, c, clen, n, pk_A, sk_B)` — public/secret key roles swap between encrypt and decrypt call sites). There is no explicit begin/commit or lock/unlock pairing since the function is stateless per call. `crypto_box_beforenm`/`crypto_box_easy_afternm`/`crypto_box_open_easy_afternm` form a co-call group: `beforenm` must be called once to derive `k`, then matching `_afternm` encrypt/decrypt calls reuse that same `k`.
**Evidence:**
> `test/default/box_easy2.c:50-54`: `crypto_box_easy(c, m, mlen, nonce, bobpk, alicesk); ... crypto_box_open_easy(m2, c, ..., nonce, alicepk, bobsk);` — encrypt uses (pk=bobpk, sk=alicesk), decrypt uses (pk=alicepk, sk=bobsk) — roles swapped, same nonce.
> `src/libsodium/include/sodium/crypto_box.h:94-108`: `crypto_box_beforenm` / `crypto_box_easy_afternm` / `crypto_box_open_easy_afternm` declared together under "/* -- Precomputation interface -- */" implying the shared-key `k` produced by `beforenm` is meant to be reused across multiple afternm calls.

### P2.8 Prerequisite State
**Claim:** Requires: (a) a valid Curve25519 keypair for the receiver (`pk`,`sk` as used by `crypto_box_open_easy`) generated by `crypto_box_keypair`/`crypto_box_seed_keypair`; (b) a ciphertext `c` of length `clen` and nonce `n` that the sender produced (ideally via `crypto_box_easy`) using the receiver's public key and the sender's secret key; (c) the sender's public key must not be of small order / degenerate (checked internally, not pre-checked by caller) else decryption fails with -1 regardless of validity of ciphertext.
**Evidence:**
> `test/default/box_easy2.c:5-9,79-87`: `small_order_p[...]` test vector; `ret = crypto_box_beforenm(k1, small_order_p, bobsk); assert(ret == -1);` — shows the library itself tests rejection of small-order keys which is the state `crypto_box_open_easy` relies on internally via `crypto_box_beforenm`.
> `src/libsodium/crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c:42-44`: `crypto_scalarmult_curve25519` returning nonzero on all-zero output (degenerate shared secret) causes `beforenm`, and thus `crypto_box_open_easy`, to fail.

### P2.9 Result Usage
**Claim:** `crypto_box_open_easy`'s only result is the plaintext written into caller-supplied `m` and the `int` success/failure code; real callers use `m` directly as application data (printed, compared with `memcmp` against the original plaintext in tests, or passed onward) only after checking the return code is `0`. `crypto_box_seal_open` is itself a consumer: it calls `crypto_box_open_easy` internally and simply forwards its return value and `m` buffer to its own caller.
**Evidence:**
> `test/default/box_easy2.c:52-58`: `if (crypto_box_open_easy(m2, c, ..., alicepk, bobsk) != 0) { printf("open() failed"); return 1; } printf("%d\n", memcmp(m, m2, mlen));` — the decrypted buffer `m2` is compared with the known plaintext `m` only after the success check.
> `src/libsodium/crypto_box/crypto_box_seal.c:61-63`: `return crypto_box_open_easy(m, c + crypto_box_PUBLICKEYBYTES, clen - crypto_box_PUBLICKEYBYTES, nonce, c, sk);` — `crypto_box_seal_open` is a direct caller/wrapper that returns the target's result and leaves `m` filled for its own caller.
> `test/default/box_seal.c:27-31`: `if (crypto_box_seal_open(m2, c, c_len, pk, sk) != 0) { printf("..failure\n"); return; } printf("%d\n", memcmp(m, m2, m_len));` — same pattern one level up through `crypto_box_seal_open`.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters: `clen` relative to `crypto_box_MACBYTES` (16) — `clen < 16` short-circuits to `-1` before any crypto; `clen == 16` is the zero-length-message decrypt path (MAC-only, no plaintext bytes); `clen > 16` is the general decrypt path, further split internally at `64 - crypto_secretbox_ZEROBYTES` bytes (block-boundary handling in `crypto_secretbox_open_detached`, lines ~114-131 of `crypto_secretbox_easy.c`) between the "block0" (poly1305-keyed + first 32 salsa20 bytes) region and the streamed remainder via `crypto_stream_salsa20_xor_ic`. Whether `pk`/`sk` form a matching/mismatched pair (and whether `pk` is a small-order point) selects the derived key `k` and therefore whether the MAC check (and thus decryption) succeeds. There is no explicit "mode" flag parameter on `crypto_box_open_easy`; mode selection across the feature happens by which entry function is called (`_easy` vs `_detached` vs `_afternm` vs `_seal_open`).
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:109-111`: `if (clen < crypto_box_MACBYTES) { return -1; }`.
> `src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:114-117`: `mlen0 = clen; if (mlen0 > 64U - crypto_secretbox_ZEROBYTES) { mlen0 = 64U - crypto_secretbox_ZEROBYTES; }` — block split boundary.
> `src/libsodium/crypto_secretbox/crypto_secretbox_easy.c:128-131`: `if (clen > mlen0) { crypto_stream_salsa20_xor_ic(m + mlen0, c + mlen0, clen - mlen0, n + 16, 1U, subkey); }` — remainder processed with a different streaming call (counter-based).
> `test/default/box_easy.c:51-58,60-69`: null-message (`mlen=0`) path exercised via `crypto_box_easy(c, guard_page, 0, ...)` then opened with `clen == crypto_box_MACBYTES`, contrasted with the tampered-MAC path that must return -1.

### P2.11 Input Expectations
**Claim:** `c` is expected to be laid out as `MAC (crypto_box_MACBYTES=16 bytes) || ciphertext_body (clen-16 bytes)`, i.e. the "easy"/combined format (MAC prefixed to the ciphertext), NOT the NaCl "boxzero"-padded format (`crypto_box()`/`crypto_box_open()` use that older, differently-padded format instead). `clen` has no documented upper bound check inside `crypto_box_open_easy` beyond the `>= MACBYTES` lower bound; on the encrypt side `crypto_box_easy` enforces `mlen <= crypto_box_MESSAGEBYTES_MAX` via `sodium_misuse()`, but `crypto_box_open_easy` does not re-check an upper bound on `clen` (relies on caller to pass a correctly sized/allocated `m` buffer of `clen - MACBYTES` bytes). `n` must be exactly `crypto_box_NONCEBYTES` (24) bytes and should be unique per (key-pair, message) to guarantee security, though `crypto_box_open_easy` doesn't validate nonce uniqueness (no state to check it). No other framing/termination bytes are expected; `c` is treated as an opaque fixed-length byte buffer, not a string (no NUL termination).
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:109-114`: `if (clen < crypto_box_MACBYTES) { return -1; } return crypto_box_open_detached(m, c + crypto_box_MACBYTES, c, clen - crypto_box_MACBYTES, n, pk, sk);` — `c`'s first MACBYTES bytes are treated as the MAC (`mac` param of `crypto_box_open_detached`), remainder as ciphertext body.
> `src/libsodium/crypto_box/crypto_box_easy.c:56-58` (`crypto_box_easy`, the producer side): `if (mlen > crypto_box_MESSAGEBYTES_MAX) { sodium_misuse(); }` — shows the size-limit enforcement exists on the encrypt side; no equivalent check appears in `crypto_box_open_easy`.
> `src/libsodium/include/sodium/crypto_box_curve25519xsalsa20poly1305.h:31`: `#define crypto_box_curve25519xsalsa20poly1305_NONCEBYTES 24U` — fixed nonce size.

## Other entries of the Logic Group (call protocol & how they combine with the target)

### crypto_box_easy — producer of valid input for the target
**Claim:** `int crypto_box_easy(unsigned char *c, const unsigned char *m, unsigned long long mlen, const unsigned char *n, const unsigned char *pk, const unsigned char *sk)`. Needs: a valid receiver `pk` and sender `sk` (from `crypto_box_keypair`), a buffer `c` of size `mlen + crypto_box_MACBYTES`, and a (ideally random, unique) nonce `n` of `crypto_box_NONCEBYTES`. `mlen` must be `<= crypto_box_MESSAGEBYTES_MAX` else triggers `sodium_misuse()` (which aborts by default). Returns 0 always in the "nonnull" happy path (no documented non-zero failure observed besides the misuse abort). In a harness: call this first with attacker-controlled `m`/`mlen` (bounded) and a valid keypair to produce a genuine ciphertext, then feed that ciphertext (optionally mutated) to `crypto_box_open_easy` with correct or swapped/corrupted keys.
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:51-61`: full body, `nonnull(1,4,5,6)` at `crypto_box.h:61-64`.
> `test/default/box_easy.c:44-45`: `ret = crypto_box_easy(c, m, 131, nonce, bobpk, alicesk); assert(ret == 0);`.

### crypto_box_detached / crypto_box_open_detached — separate-MAC variant
**Claim:** `crypto_box_detached(c, mac, m, mlen, n, pk, sk)` writes ciphertext to `c` (size `mlen`) and the 16-byte MAC separately to `mac` (size `crypto_box_MACBYTES`). `crypto_box_open_detached(m, c, mac, clen, n, pk, sk)` is the exact decrypt counterpart the target (`crypto_box_open_easy`) itself calls internally after splitting `c`. Both require the same matching keypair/nonce discipline as the `_easy` variants. Returns 0/-1 like the target. In a harness exercising this variant, `crypto_box_open_detached` can be called directly with a fuzzer-supplied `mac` buffer (decoupled from `c`), giving a different mutation surface (e.g. valid ciphertext + corrupted mac, or valid mac + corrupted ciphertext) compared to the combined buffer the target uses.
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_easy.c:20-37,73-89`: definitions; `crypto_box_open_easy` itself calls `crypto_box_open_detached` (`crypto_box_easy.c:112-114`) confirming it is the same underlying mechanism with buffers merged.
> `src/libsodium/include/sodium/crypto_box.h:72-86`: `nonnull(1,2,5,6,7)` for detached, `nonnull(2,3,5,6,7)` for open_detached.

### crypto_box_beforenm / crypto_box_easy_afternm / crypto_box_open_easy_afternm — precomputed-key variants
**Claim:** `crypto_box_beforenm(k, pk, sk)` derives the 32-byte (`crypto_box_BEFORENMBYTES`) shared session key `k` once via ECDH+HSalsa20; returns -1 on degenerate/small-order `pk`. `crypto_box_easy_afternm(c, m, mlen, n, k)` / `crypto_box_open_easy_afternm(m, c, clen, n, k)` then reuse that precomputed `k` instead of `pk`/`sk`, skipping the scalarmult step — useful for repeated exchanges with the same peer. `crypto_box_open_easy_afternm` has the identical `clen < crypto_box_MACBYTES` guard as the target. Must zero `k` after use (caller responsibility, since there is no object to "destroy" — it's a raw byte buffer the caller owns). In a harness: call `crypto_box_beforenm` once after keypair generation, then drive many `_afternm` encrypt/decrypt round trips reusing `k`, exercising the "config once, use many" pattern the target's family supports.
**Evidence:**
> `src/libsodium/crypto_box/crypto_box.c:77-82`: `crypto_box_beforenm` thin wrapper to `crypto_box_curve25519xsalsa20poly1305_beforenm`.
> `src/libsodium/crypto_box/crypto_box_easy.c:39-49,91-102`: `crypto_box_easy_afternm`, `crypto_box_open_easy_afternm` definitions, both `nonnull` per `crypto_box.h:100-108`.
> `test/default/box_easy2.c:79-87`: `crypto_box_beforenm(k1, alicepk, bobsk); assert(ret == 0);` paired `k2` computed the opposite direction, confirming the key-reuse pattern.

### crypto_box_seal / crypto_box_seal_open — ephemeral-sender anonymous variant
**Claim:** `crypto_box_seal(c, m, mlen, pk)` internally calls `crypto_box_keypair` to generate an ephemeral keypair, derives a deterministic nonce via BLAKE2b (`crypto_generichash`) over `epk||pk`, then calls `crypto_box_easy` with the ephemeral secret key; it writes `epk (32 bytes) || ciphertext+mac (mlen+16 bytes)` into `c` (total `crypto_box_SEALBYTES + mlen`). `crypto_box_seal_open(m, c, clen, pk, sk)` requires `clen >= crypto_box_SEALBYTES` (48), re-derives the same nonce from `c`'s leading 32 bytes (`epk`) and `pk`, then calls `crypto_box_open_easy(m, c+32, clen-32, nonce, c /*epk*/, sk)` — i.e. it is a direct, real caller of the target. No persistent object; ephemeral `esk` is zeroed internally after use (`crypto_box_seal.c:43`). In a harness: `crypto_box_keypair` → `crypto_box_seal` → `crypto_box_seal_open` exercises the target through this wrapper with attacker-mutatable `c` and `pk`/`sk`.
**Evidence:**
> `src/libsodium/crypto_box/crypto_box_seal.c:24-46`: `crypto_box_seal` body (keypair gen, nonce derivation, `crypto_box_easy` call, `sodium_memzero(esk,...)`).
> `src/libsodium/crypto_box/crypto_box_seal.c:48-64`: `crypto_box_seal_open` body, `if (clen < crypto_box_SEALBYTES) return -1;` then calls `crypto_box_open_easy(m, c + crypto_box_PUBLICKEYBYTES, clen - crypto_box_PUBLICKEYBYTES, nonce, c, sk);` — direct call to the target.
> `test/default/box_seal.c:16,23,27`: `crypto_box_keypair(pk, sk); ... crypto_box_seal(c, m, m_len, pk); ... crypto_box_seal_open(m2, c, c_len, pk, sk);`.

### crypto_box_keypair / crypto_box_seed_keypair — key material producers
**Claim:** `crypto_box_keypair(pk, sk)` fills `pk` (32 bytes) and `sk` (32 bytes) using `randombytes_buf` + `crypto_scalarmult_curve25519_base`; always returns 0 in practice (wraps a call chain with no documented failure for this path). `crypto_box_seed_keypair(pk, sk, seed)` derives the keypair deterministically from a 32-byte `seed` via SHA-512 — useful for a harness wanting reproducible/fuzzer-controlled (but still valid, non-corrupted) key material instead of calling `randombytes_buf`. Both are prerequisites that must run before any `crypto_box_easy`/`crypto_box_open_easy`/`*_detached`/`*_afternm`/`*_seal*` call that needs a real (non-adversarial) keypair. No cleanup required beyond the caller freeing/zeroing `sk` when done.
**Evidence:**
> `src/libsodium/crypto_box/crypto_box.c:64-75`: `crypto_box_seed_keypair` / `crypto_box_keypair` wrappers to `crypto_box_curve25519xsalsa20poly1305_{seed_keypair,keypair}`.
> `src/libsodium/crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c:11-32`: `crypto_box_curve25519xsalsa20poly1305_seed_keypair` (SHA-512 of seed → sk, then `crypto_scalarmult_curve25519_base`) and `crypto_box_curve25519xsalsa20poly1305_keypair` (`randombytes_buf(sk,32); return crypto_scalarmult_curve25519_base(pk,sk);`).
> `test/default/box_easy2.c:45-46`: `crypto_box_keypair(alicepk, alicesk); crypto_box_keypair(bobpk, bobsk);` called before any easy/detached call.
> `src/libsodium/include/sodium/crypto_box.h:51-58`: both declared `__attribute__((nonnull))`.

## Build notes
**Claim:** Fuzz harnesses for this project are compiled as C++ (`-std=c++11`) translation units named `*_fuzzer.cc`, including the top-level umbrella header `<sodium.h>` (which pulls in `crypto_box.h` and all other module headers), compiled with an include path pointing at the libsodium source tree's public include directory, and statically linked against the built `libsodium.a` plus `$LIB_FUZZING_ENGINE`. `sodium_init()` is called as the very first statement in `LLVMFuzzerTestOneInput` in the sibling easy-box-family fuzzer (`secretbox_easy_fuzzer.cc`), confirming the project's convention for this feature group.
**Evidence:**
> `ossfuzz_view/libsodium-c2/build.sh:19-27`:
> ```
> ./configure --enable-static LDFLAGS="$CXXFLAGS"
> make -j$(nproc) all
> for f in $SRC/*_fuzzer.cc; do
>     fuzzer=$(basename "$f" _fuzzer.cc)
>     $CXX $CXXFLAGS -std=c++11 -I"$SRC/libsodium/src/libsodium/include" \
>          "$f" -o "$OUT/${fuzzer}_fuzzer" \
>          "$SRC/libsodium/src/libsodium/.libs/libsodium.a" $LIB_FUZZING_ENGINE
> done
> ```
> `ossfuzz_view/libsodium-c2/secretbox_easy_fuzzer.cc:17-25`: `#include <sodium.h>` ... `extern "C" int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) { int initialized = sodium_init(); assert(initialized >= 0); setup_fake_random(data, size); ... }`.
> `src/libsodium-c2/libsodium/src/libsodium/include/sodium.h` exists as the umbrella header pulled via the `-I.../include` path (confirmed present via Glob of `src/libsodium/include/sodium.h`).
> `ossfuzz_view/libsodium-c2/crypto_box_fuzzer.cc:1-16`: current gold harness for this target is a stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps `crypto_box_open_easy` linked via an `__asm__` alias trick and does not actually call it — the real call pattern must be reconstructed from the project's own tests (`test/default/box_easy.c`, `box_easy2.c`, `box_seal.c`) and the sibling `secretbox_easy_fuzzer.cc`, as cited above.
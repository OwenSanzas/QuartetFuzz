## target: crypto_sign_verify_detached

### Summary of dispatch chain
`crypto_sign_verify_detached` (sodium/crypto_sign.h:77-82, implemented src/libsodium/crypto_sign/crypto_sign.c:83-88) is a thin wrapper:
```
crypto_sign.c:84  int crypto_sign_verify_detached(sig, m, mlen, pk)
crypto_sign.c:87    return crypto_sign_ed25519_verify_detached(sig, m, mlen, pk);
```
which in turn (src/libsodium/crypto_sign/ed25519/ref10/open.c:59-66) calls the core:
```
open.c:65  return _crypto_sign_ed25519_verify_detached(sig, m, mlen, pk, 0 /* prehashed */);
```
Core body (open.c:14-57) performs, in order: `sc25519_is_canonical(sig+32)` and `ge25519_has_small_order(sig)` (unless `ED25519_COMPAT`), `ge25519_is_canonical(pk)` and `ge25519_has_small_order(pk)`, `ge25519_frombytes_negate_vartime(&A, pk)`, SHA-512(sig[0:32]||pk||m) → `h`, `sc25519_reduce(h)`, `ge25519_double_scalarmult_vartime`, `ge25519_tobytes`, then compares `rcheck` to `sig[0:32]` via `crypto_verify_32` and `sodium_memcmp`. Returns 0 on success, -1 on any failure (bad canonical form, small-order point, mismatch).

### P2.1 Init Sequence
**Claim:** No mandatory call is required by the function itself (no internal lazy global state check), but every real caller in this project first calls `sodium_init()` once at process start, and calls `crypto_sign_keypair`/`crypto_sign_seed_keypair` to obtain `pk` and a matching `crypto_sign_detached` (or the streaming `crypto_sign_final_create`) to obtain `sig`, before calling `crypto_sign_verify_detached`.
**Evidence:**
> sodium/crypto_sign.h:5-8: "THREAD SAFETY: crypto_sign_keypair() is thread-safe, provided that sodium_init() was called before. Other functions ... are always thread-safe."
> ossfuzz_view/libsodium-c1/crypto_box_fuzzer.cc:22-23: `int initialized = sodium_init(); assert(initialized >= 0);` (sibling fuzzer in same project, same convention)
> ossfuzz_view/libsodium-c1/fake_random.h:61-62: `int initialized = sodium_init(); assert(initialized >= 0);` inside `setup_fake_random()` used by all sibling fuzzers
> test/default/sign.c:1130-1149: `crypto_sign_detached(...)` called, then immediately `crypto_sign_verify_detached(sig, m, i, test_data[i].pk)` on the produced signature and the matching public key.

### P2.2 Parameter Construction
**Claim:** `sig` must point to (at least) `crypto_sign_BYTES` (64) readable bytes — layout is `R(32 bytes) || s(32 bytes)`; `m` points to `mlen` bytes (may be NULL only if mlen==0, but `__attribute__((nonnull(1,4)))` only forces sig and pk non-null, not m); `mlen` is attacker length, must be `<= crypto_sign_MESSAGEBYTES_MAX`; `pk` must point to `crypto_sign_PUBLICKEYBYTES` (32) bytes, a compressed Ed25519 point (not necessarily on curve — code checks and rejects).
**Evidence:**
> sodium/crypto_sign.h:77-82: `nonnull(1,4)` attribute — sig and pk must not be NULL; m/mlen unconstrained by the attribute.
> sodium/crypto_sign.h:28, 36: `#define crypto_sign_BYTES crypto_sign_ed25519_BYTES` (=64), `#define crypto_sign_PUBLICKEYBYTES crypto_sign_ed25519_PUBLICKEYBYTES` (=32).
> sodium/crypto_sign_ed25519.h:22, 30, 38: `crypto_sign_ed25519_BYTES 64U`, `crypto_sign_ed25519_PUBLICKEYBYTES 32U`, `crypto_sign_ed25519_MESSAGEBYTES_MAX (SODIUM_SIZE_MAX - crypto_sign_ed25519_BYTES)`.
> crypto_sign/ed25519/ref10/open.c:33-40: `sc25519_is_canonical(sig+32)`, `ge25519_has_small_order(sig)` (sig interpreted as point||scalar at fixed 32/32 split), `ge25519_is_canonical(pk)`, `ge25519_has_small_order(pk)` — all read exactly 32 bytes from fixed offsets, no length parameter for sig/pk (the API hard-codes 64/32-byte buffers).

### P2.3 Object Lifecycle
**Claim:** There is no opaque object to create/destroy for the one-shot API: `crypto_sign_verify_detached` is stateless given its 4 arguments (sig, m, mlen, pk are all caller-owned plain buffers). No "destroy"/"free" call exists or is needed for its own inputs/outputs (it returns only an `int`). The streaming variant (`crypto_sign_init`/`update`/`final_verify`) does use a stack-sized `crypto_sign_state` object with create→update*→final lifecycle (see below).
**Evidence:**
> crypto_sign.c:83-88: function signature returns `int`, takes no pointer-out parameter besides the already-caller-allocated buffers.
> sodium/crypto_sign.h:99-101: `crypto_sign_final_verify(crypto_sign_state *state, ...)` — the only object-lifecycle entry related to verification, distinct from the target.

### P2.4 Return Value Handling
**Claim:** Return `0` means signature is valid for (m, pk); return `-1` means invalid/rejected (canonical/small-order check failure, or MAC mismatch). `__attribute__((warn_unused_result))` forces caller to check. The caller must NOT treat the verified message `m` as "untouched/authentic" unless return is exactly 0; libsodium does not throw or set errno.
**Evidence:**
> sodium/crypto_sign.h:77-82: `__attribute__ ((warn_unused_result))`.
> crypto_sign/ed25519/ref10/open.c:29-44: multiple `return -1;` early-out paths for invalid encodings.
> crypto_sign/ed25519/ref10/open.c:55-56: `return crypto_verify_32(rcheck, sig) | (-(rcheck == sig)) | sodium_memcmp(sig, rcheck, 32);` — final combined comparison, 0 only if both checks (and pointer-aliasing guard) pass.
> test/default/sign.c:1144-1149: caller treats non-zero as failure ("detached signature verification failed").
> test/default/sign.c:1166-1174: negative test flips bits in sig and asserts result `!= -1` is a test failure, i.e. expects exactly -1 on tamper.

### P2.5 Cleanup Sequence
**Claim:** No cleanup of `crypto_sign_verify_detached`'s own parameters is required beyond normal buffer lifetime management by the caller (sig/m/pk are not owned/freed by the function). If the caller generated `sk` for the signing side, it should be wiped with `sodium_memzero`/`sodium_memzero`-wrapping as done inside libsodium's own key generation helpers, but that's for the `sk`, not consumed by the target itself.
**Evidence:**
> crypto_sign/ed25519/ref10/sign.c:87-88 (`_crypto_sign_ed25519_detached`): `sodium_memzero(az, sizeof az); sodium_memzero(nonce, sizeof nonce);` — pattern used by the library itself to scrub ephemeral secret material; not applicable inside the verify path (no secret-derived intermediate retained beyond local stack vars `h`,`rcheck`,`A`,`R` which the core function does NOT scrub — these are not secrets, so no zeroing call appears in open.c).

### P2.6 API Existence
**Claim:** All entries listed below exist verbatim in this codebase at the cited locations.
**Evidence:**
> sodium/crypto_sign.h:53-101 declares: `crypto_sign_seed_keypair` (53), `crypto_sign_keypair` (57), `crypto_sign_open` (66), `crypto_sign_detached` (72), `crypto_sign_verify_detached` (77), `crypto_sign_init` (84), `crypto_sign_update` (87), `crypto_sign_final_create` (92), `crypto_sign_final_verify` (98) — all present.
> crypto_sign.c:46-115 defines all of the above as thin wrappers to the `_ed25519` family.

### P2.7 Co-call Constraints
**Claim:** `crypto_sign_verify_detached` must be paired with a prior `crypto_sign_detached` (or `crypto_sign`/`crypto_sign_final_create`) that produced `sig` over the same `(m, mlen)` and the `pk` that corresponds to the `sk` used to sign — otherwise verification legitimately fails. For the streaming/prehashed API, `crypto_sign_init` → `crypto_sign_update`(*) → `crypto_sign_final_verify` is a strict begin/update*/end triple that must mirror the signer's `crypto_sign_init` → `crypto_sign_update`(*) → `crypto_sign_final_create` triple (same message chunks reassembled, same `state` type, not reused across sign and verify).
**Evidence:**
> test/default/sign.c:1232-1240:
> ```
> crypto_sign_init(&st); crypto_sign_update(&st, m, i); crypto_sign_final_create(&st, sig, NULL, sk);
> crypto_sign_init(&st); crypto_sign_update(&st, m, i); if (crypto_sign_final_verify(&st, sig, pk) != 0) ...
> ```
> Shows a fresh `crypto_sign_init` is required before `crypto_sign_final_verify`, i.e. state cannot be reused post-finalize.
> test/default/sign.c:1243-1248: two `crypto_sign_update` calls split the same message into chunks before one `crypto_sign_final_verify`, showing update is repeatable (0..n times).

### P2.8 Prerequisite State
**Claim:** The only prerequisite state is: (a) the process should have called `sodium_init()` (per the thread-safety note, this is advisory for `crypto_sign_verify_detached`/`_detached` itself since they don't call `randombytes`, but is required if `crypto_sign_keypair` — using the RNG — is used in the same harness to produce `pk`); (b) `pk` must be 32 bytes and `sig` must be 64 bytes, both fully initialized (not partially written) before the call, since the core reads fixed 32/64-byte windows unconditionally.
**Evidence:**
> sodium/crypto_sign.h:5-8 (thread-safety note tying `sodium_init` to `crypto_sign_keypair`).
> crypto_sign/ed25519/ref10/open.c:33-40: unconditional reads of `sig+32`, `sig` (first 32), `pk` as fixed 32-byte windows — no length argument guards these reads (only `mlen` is a length argument, for `m`).

### P2.9 Result Usage
**Claim:** Within this project, the project does not feed the `int` return value of `crypto_sign_verify_detached` into any further API (it's a boolean-like accept/reject value consumed directly by application logic: `if (crypto_sign_verify_detached(...) != 0) { reject; } else { treat m as authentic for further processing }`). The sibling `crypto_sign_open` internally calls the ed25519 verify_detached variant and, on success, additionally copies the verified message out via `memmove(m, sm+64, mlen)` (open.c:88-90) — i.e. `crypto_sign_open`'s "message output buffer" is the downstream consumer pattern for the detached-verify-equivalent result.
**Evidence:**
> test/default/sign.c:1144-1149: `if (crypto_sign_verify_detached(...) != 0) { printf(...); continue; }` — direct branch on return value, no further API call on the int itself.
> crypto_sign/ed25519/ref10/open.c:79-90: `if (crypto_sign_ed25519_verify_detached(sm, sm + 64, mlen, pk) != 0) { if (m != NULL) memset(m, 0, mlen); goto badsig; } ... if (m != NULL) { memmove(m, sm + 64, mlen); }` — shows the established pattern: on failure, zero out any output buffer; on success, copy the message to the output.

### P2.10 Behaviour Controls
**Claim:** The only "mode" switch reaching the shared core `_crypto_sign_ed25519_verify_detached` is the `prehashed` flag (0 for `crypto_sign_verify_detached`/`crypto_sign_open`, 1 for `crypto_sign_final_verify`), which changes the SHA-512 domain-separation prefix (`_crypto_sign_ed25519_ref10_hinit`) and expects `m`/`mlen` to already be the Ed25519ph prehash (64-byte SHA-512 digest) rather than the raw message. Compile-time macro `ED25519_COMPAT` changes which checks run (skips canonicality/small-order checks, replaces with a weaker `sig[63] & 224` check) — this is a build-time, not run-time, control.
**Evidence:**
> crypto_sign/ed25519/ref10/open.c:15-19,28-41: `int prehashed` parameter; `#ifdef ED25519_COMPAT ... #else sc25519_is_canonical/ge25519_has_small_order/ge25519_is_canonical ... #endif`.
> crypto_sign/ed25519/sign_ed25519.c:92-96: `crypto_sign_ed25519ph_final_verify` passes `prehashed=1` and `ph` (the finalized SHA-512 digest) as `m`/`mlen=64`.
> crypto_sign/ed25519/ref10/open.c:65: `crypto_sign_ed25519_verify_detached` (used by the target) always passes `prehashed=0`.

### P2.11 Input Expectations
**Claim:** `sig` is exactly 64 bytes: bytes[0:32] = compressed R point, bytes[32:64] = scalar s. `pk` is exactly 32 bytes: compressed Ed25519 point. `m`/`mlen` is an arbitrary byte string (attacker-controlled), with the only documented limit `mlen <= crypto_sign_MESSAGEBYTES_MAX` (`SODIUM_SIZE_MAX - 64`); there's no header/magic/termination inside `m` — it is hashed raw via SHA-512 together with sig[0:32] and pk. The function itself does not check `mlen` against `MESSAGEBYTES_MAX` (that check is only done by `crypto_sign_open`'s length-prefixed wrapper); the raw detached API trusts the caller's `mlen` value and will process any size (subject to `unsigned long long` overflow concerns in the hashing loop, mitigated by SHA-512's internal 64-bit length counter). Zero-length `m` (mlen==0) is valid (test harness exercises `i` from 0 upward, see `for (i = 0U; ...)` at sign.c:1081 which starts verifying messages of length 0).
**Evidence:**
> sodium/crypto_sign_ed25519.h:38: `#define crypto_sign_ed25519_MESSAGEBYTES_MAX (SODIUM_SIZE_MAX - crypto_sign_ed25519_BYTES)`.
> crypto_sign/ed25519/ref10/open.c:46-48: `crypto_hash_sha512_update(&hs, sig, 32); crypto_hash_sha512_update(&hs, pk, 32); crypto_hash_sha512_update(&hs, m, mlen);` — raw concatenation hash, no length/format markers inside `m`.
> crypto_sign/ed25519/ref10/open.c:75-78 (inside `crypto_sign_ed25519_open`, the length-checking sibling): `if (smlen < 64 || smlen - 64 > crypto_sign_ed25519_MESSAGEBYTES_MAX) { goto badsig; }` — shows the MESSAGEBYTES_MAX bound is enforced by the *bundled* API, not inside the detached-verify core itself.
> test/default/sign.c:1081: `for (i = 0U; i < (sizeof test_data)/(sizeof test_data[0]); i++)` with `i` used directly as `mlen` down to 0, confirms 0-length message is an accepted/tested input.

## Other entries of the Logic Group (protocol + combination with the target)

### crypto_sign_keypair(unsigned char *pk, unsigned char *sk)
**Declaration:** sodium/crypto_sign.h:57-59, `nonnull` both params, no return-value attribute (still returns int, always 0 here).
**Call protocol:** Needs `sodium_init()` called first for the thread-safety guarantee (see P2.1 evidence); allocates nothing itself — caller must supply `pk[crypto_sign_PUBLICKEYBYTES]` (32) and `sk[crypto_sign_SECRETKEYBYTES]` (64) buffers. Internally calls `randombytes_buf` to pick a 32-byte seed then delegates to `crypto_sign_ed25519_seed_keypair`. Returns `0` always (ref10 keypair.c:33-43 — no error path). Nothing to free (stack/caller buffers only); secrets inside `sk` should eventually be wiped by caller with `sodium_memzero` if no longer needed, though the library doesn't force this.
**Combination with target:** Produces `pk` consumed as the 4th argument of `crypto_sign_verify_detached`; `sk` is consumed by `crypto_sign_detached`/`crypto_sign`/`crypto_sign_final_create` to produce `sig`.
**Evidence:**
> sodium/crypto_sign.h:57-59: `int crypto_sign_keypair(unsigned char *pk, unsigned char *sk) __attribute__ ((nonnull));`
> crypto_sign/ed25519/ref10/keypair.c:32-43: `randombytes_buf(seed, sizeof seed); ret = crypto_sign_ed25519_seed_keypair(pk, sk, seed); sodium_memzero(seed, sizeof seed); return ret;`
> test/default/sign.c:1282-1283: `if (crypto_sign_keypair(pk, sk) != 0) { printf("crypto_sign_keypair() failure\n"); }`

### crypto_sign_seed_keypair(unsigned char *pk, unsigned char *sk, const unsigned char *seed)
**Declaration:** sodium/crypto_sign.h:52-55, `nonnull` all 3 params.
**Call protocol:** Deterministic variant — `seed` must be `crypto_sign_SEEDBYTES` (32) attacker/fuzz-controlled bytes. Always thread-safe without requiring `sodium_init()` (per header comment), does not call `randombytes`. Returns 0 always (ref10 keypair.c:12-30, no error path: `crypto_hash_sha512(sk, seed, 32); ... ge25519_scalarmult_base(&A, sk); ge25519_p3_tobytes(pk, &A); memmove(sk, seed, 32); memmove(sk+32, pk, 32); return 0;`). Nothing to free.
**Combination with target:** Same as `crypto_sign_keypair` — gives a `(pk, sk)` pair that can be driven entirely from fuzz bytes (via `seed`), making the generated `pk` the 4th arg to `crypto_sign_verify_detached` and `sk` the signing key for `crypto_sign_detached`/`crypto_sign_final_create`.
**Evidence:**
> sodium/crypto_sign.h:52-55: `int crypto_sign_seed_keypair(unsigned char *pk, unsigned char *sk, const unsigned char *seed) __attribute__ ((nonnull));`
> sodium/crypto_sign.h:5-9: "Other functions, including crypto_sign_seed_keypair() are always thread-safe" — no required sodium_init for this one.
> crypto_sign/ed25519/ref10/keypair.c:12-30 (definition, full body quoted above).
> test/default/sign.c:1228-1229: `if (crypto_sign_seed_keypair(pk, sk, keypair_seed) != 0) { printf("crypto_sign_seed_keypair() failure\n"); }`

### crypto_sign_detached(unsigned char *sig, unsigned long long *siglen_p, const unsigned char *m, unsigned long long mlen, const unsigned char *sk)
**Declaration:** sodium/crypto_sign.h:72-75, `nonnull(1,5)` (sig and sk required; m may be NULL iff mlen==0; siglen_p may be NULL).
**Call protocol:** `sig` output buffer must have room for `crypto_sign_BYTES` (64) bytes; `siglen_p` optional (pass NULL to skip, as in test/default/sign.c:1198); `sk` is the 64-byte secret key from `crypto_sign_keypair`/`crypto_sign_seed_keypair`. Internally derives nonce from SHA-512(sk[0:32]) plus message, computes R, then s = h*a + r (ref10/sign.c:49-94). Always returns 0 (no error path shown). On success, `*siglen_p` (if non-NULL) is set to 64.
**Combination with target:** This is the direct producer of `sig` for `crypto_sign_verify_detached`; in a fuzz harness the natural pairing is: generate (pk, sk) → `crypto_sign_detached(sig, &siglen, m, mlen, sk)` → optionally mutate `sig`/`m`/`pk` from fuzz data → `crypto_sign_verify_detached(sig, m, mlen, pk)`.
**Evidence:**
> sodium/crypto_sign.h:72-75: declaration with `nonnull(1,5)`.
> crypto_sign/ed25519/ref10/sign.c:49-94 (`_crypto_sign_ed25519_detached`): full signing body, `if (siglen_p != NULL) { *siglen_p = 64U; } return 0;` (lines 90-93).
> test/default/sign.c:1130-1143: `crypto_sign_detached(sig, &siglen, m, i, skpk)` immediately followed by length/content checks then `crypto_sign_verify_detached(sig, m, i, pk)`.

### crypto_sign_open(unsigned char *m, unsigned long long *mlen_p, const unsigned char *sm, unsigned long long smlen, const unsigned char *pk)
**Declaration:** sodium/crypto_sign.h:66-70, `warn_unused_result`, `nonnull(3,5)` (sm, pk required; m/mlen_p optional).
**Call protocol:** `sm` = signature||message bundle, `smlen` is its total length; requires `smlen >= 64` else immediate failure (`goto badsig`). Needs a `pk` from keypair generation. Internally calls `crypto_sign_ed25519_verify_detached(sm, sm+64, smlen-64, pk)` — i.e. reaches the exact same core as the target but via a bundled buffer and with an extra length precheck. On success, copies the message (`sm+64 .. sm+64+mlen`) into caller-owned `m` and sets `*mlen_p = mlen`. On failure: zeroes `m` (if non-NULL) and sets `*mlen_p = 0`, returns -1.
**Combination with target:** Alternative "bundled" variant exercising a length-check path (`smlen < 64`, `smlen - 64 > crypto_sign_ed25519_MESSAGEBYTES_MAX`) not present in the bare detached API; same security-critical core is reached. Useful to fuzz the smlen boundary (0..63 truncated case) that the detached API does not itself check.
**Evidence:**
> sodium/crypto_sign.h:66-70: declaration.
> crypto_sign/ed25519/ref10/open.c:68-98: `crypto_sign_ed25519_open` body, including `if (smlen < 64 || smlen - 64 > crypto_sign_ed25519_MESSAGEBYTES_MAX) goto badsig;` (line 75) and `crypto_sign_ed25519_verify_detached(sm, sm + 64, mlen, pk)` (line 79).
> test/default/sign.c:1124-1129: `crypto_sign_open(m, &mlen, sm, i % crypto_sign_BYTES, pk) == 0` test expecting short-signed-message rejection.

### crypto_sign_init(crypto_sign_state *state)
**Declaration:** sodium/crypto_sign.h:84-85. `crypto_sign_state` is `typedef crypto_sign_ed25519ph_state` (sodium/crypto_sign.h:23), a struct wrapping a `crypto_hash_sha512_state`.
**Call protocol:** Caller allocates `state` (stack or heap, size given by `crypto_sign_statebytes()`); no prior init needed beyond the allocation. Always returns 0 (`crypto_hash_sha512_init(&state->hs); return 0;`, sign_ed25519.c:61-65). Must be called once before any `crypto_sign_update`/`crypto_sign_final_create`/`crypto_sign_final_verify` on that state, and again (fresh) to start a new session (state cannot be reused after a `final_*` call without re-`init`).
**Evidence:**
> sodium/crypto_sign.h:23: `typedef crypto_sign_ed25519ph_state crypto_sign_state;`
> crypto_sign/ed25519/sign_ed25519.c:60-65: `crypto_sign_ed25519ph_init` body.
> test/default/sign.c:1232,1238,1243: three separate `crypto_sign_init(&st);` calls, each starting a fresh session.

### crypto_sign_update(crypto_sign_state *state, const unsigned char *m, unsigned long long mlen)
**Declaration:** sodium/crypto_sign.h:87-90, `nonnull(1)` (state required; m may be NULL iff mlen==0).
**Call protocol:** Must be called after `crypto_sign_init` on the same `state`; can be called 0+ times to feed message chunks incrementally (absorbed into running SHA-512 state). Returns whatever `crypto_hash_sha512_update` returns (0 on success). No dedicated free.
**Combination with target:** Chains into `crypto_sign_final_verify`, which reaches the identical core as the target but with `prehashed=1` and `m` replaced by the finalized 64-byte SHA-512 digest.
**Evidence:**
> sodium/crypto_sign.h:87-90: declaration.
> crypto_sign/ed25519/sign_ed25519.c:67-72: `crypto_sign_ed25519ph_update` — `return crypto_hash_sha512_update(&state->hs, m, mlen);`
> test/default/sign.c:1243-1247: two chained `crypto_sign_update` calls splitting one message into halves before `crypto_sign_final_verify`.

### crypto_sign_final_create(crypto_sign_state *state, unsigned char *sig, unsigned long long *siglen_p, const unsigned char *sk)
**Declaration:** sodium/crypto_sign.h:92-96, `nonnull(1,2,4)` (state, sig, sk required; siglen_p optional).
**Call protocol:** Call after `crypto_sign_init`+`crypto_sign_update`* on `state`, with the signer's secret key `sk`. Finalizes the SHA-512 state into a 64-byte prehash `ph`, then calls `_crypto_sign_ed25519_detached(sig, siglen_p, ph, 64, sk, prehashed=1)`. `state` is consumed/finalized by this call (its internal hash state is fully drained by `crypto_hash_sha512_final`) — must `crypto_sign_init` again before reusing the same `state` variable. Writes 64 bytes to `sig`.
**Combination with target:** Produces a `sig` whose matching verification counterpart is `crypto_sign_final_verify` (NOT the plain `crypto_sign_verify_detached`, since prehashed=1 changes the SHA-512 domain prefix) — mixing a `final_create`-produced signature into the plain `crypto_sign_verify_detached` call is expected to fail (different hash prefix), which is itself a useful negative-path pairing to fuzz.
**Evidence:**
> sodium/crypto_sign.h:92-96: declaration.
> crypto_sign/ed25519/sign_ed25519.c:74-85: `crypto_sign_ed25519ph_final_create` body: `crypto_hash_sha512_final(&state->hs, ph); return _crypto_sign_ed25519_detached(sig, siglen_p, ph, sizeof ph, sk, 1);`
> test/default/sign.c:1232-1234: `crypto_sign_init(&st); crypto_sign_update(&st, m, i); crypto_sign_final_create(&st, sig, NULL, sk);`

### crypto_sign_final_verify(crypto_sign_state *state, const unsigned char *sig, const unsigned char *pk)
**Declaration:** sodium/crypto_sign.h:98-101 (truncated in first read, but signature confirmed via sign_ed25519.c), `warn_unused_result` per crypto_sign.h pattern (same style as target — confirm nonnull below).
**Call protocol:** Call after `crypto_sign_init`+`crypto_sign_update`* with the same chunking the signer used. Finalizes the hash into `ph` and calls `_crypto_sign_ed25519_verify_detached(sig, ph, 64, pk, prehashed=1)` — the SAME core function as the target, just with `prehashed=1`. Returns 0/-1 exactly like the target. `state` is consumed; must re-`crypto_sign_init` to reuse.
**Combination with target:** This is the prehashed sibling of the target reaching the identical core (`_crypto_sign_ed25519_verify_detached`) — a harness exercising both `crypto_sign_verify_detached` (prehashed=0 path) and `crypto_sign_init/update/final_verify` (prehashed=1 path) covers both branches of the shared core's dependency on the `prehashed` flag (only affects the SHA-512 domain prefix, not the canonicality/small-order checks which run identically in both cases).
**Evidence:**
> crypto_sign/ed25519/sign_ed25519.c:87-97: `crypto_sign_ed25519ph_final_verify` body: `crypto_hash_sha512_final(&state->hs, ph); return _crypto_sign_ed25519_verify_detached(sig, ph, sizeof ph, pk, 1);`
> test/default/sign.c:1238-1257: `crypto_sign_init(&st); crypto_sign_update(&st, m, i); if (crypto_sign_final_verify(&st, sig, pk) != 0) ...` followed by negative tests reusing `sig` against different/updated `st` expecting `-1`.

## Build notes
**Claim:** Fuzzers for this project are compiled as C++ (`.cc`), including the installed public umbrella header `<sodium.h>` (which pulls in `crypto_sign.h` transitively), linked statically against `libsodium.a` built by `./configure && make`. No special libraries beyond `$LIB_FUZZING_ENGINE`.
**Evidence:**
> ossfuzz_view/libsodium-c1/build.sh:19-20: `./configure --enable-static LDFLAGS="$CXXFLAGS"` then `make -j$(nproc) all`.
> ossfuzz_view/libsodium-c1/build.sh:23-27:
> ```
> for f in $SRC/*_fuzzer.cc; do
>   fuzzer=$(basename "$f" _fuzzer.cc)
>   $CXX $CXXFLAGS -std=c++11 -I"$SRC/libsodium/src/libsodium/include" \
>        "$f" -o "$OUT/${fuzzer}_fuzzer" \
>        "$SRC/libsodium/src/libsodium/.libs/libsodium.a" $LIB_FUZZING_ENGINE
> done
> ```
> → fuzzer sources are `.cc` (C++11), include path is `src/libsodium/include` (the directory containing the public installed headers e.g. `sodium.h`, `sodium/crypto_sign.h`), statically linked against the just-built `libsodium.a`.
> ossfuzz_view/libsodium-c1/crypto_box_fuzzer.cc:17: `#include <sodium.h>` — sibling fuzzer confirms the umbrella header is the intended include, not the internal `crypto_sign.h` path directly.
> ossfuzz_view/libsodium-c1/Dockerfile:19: `git clone --depth 1 https://github.com/jedisct1/libsodium.git --branch stable libsodium` — source fetched fresh; `COPY run_tests.sh build.sh *.cc *.h $SRC/` (line 21) copies fuzzer sources and `fake_random.h` into `$SRC`.
> Current `crypto_sign_fuzzer.cc` in the view (ossfuzz_view/libsodium-c1/crypto_sign_fuzzer.cc:1-16) is a placeholder/stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps the symbol linked via `__asm__("crypto_sign_verify_detached")` and does not call it — this is NOT a real usage example, unlike `crypto_box_fuzzer.cc`/`fake_random.h` which show the real sodium_init + keypair + sign/verify pattern used elsewhere in the same OSS-Fuzz directory.